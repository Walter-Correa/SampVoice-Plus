/*
 * SampVoice Plus - Cross-Platform High-Throughput UDP Socket Implementation
 */

#include "svplus/common/UdpSocket.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>
    #pragma comment(lib, "Ws2_32.lib")
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace svplus::net {

namespace {

#ifdef _WIN32
class WsaLifecycle {
public:
    static bool Acquire() noexcept {
        if (refCount_.fetch_add(1, std::memory_order_acq_rel) == 0) {
            WSADATA wsaData{};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                refCount_.fetch_sub(1, std::memory_order_acq_rel);
                return false;
            }
        }
        return true;
    }

    static void Release() noexcept {
        if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            WSACleanup();
        }
    }

private:
    static inline std::atomic<int> refCount_{0};
};

inline bool IsWouldBlock(int err) noexcept {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}
#else
inline bool IsWouldBlock(int err) noexcept {
    return err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS;
}
#endif

} // namespace

bool UdpEndpoint::FromString(const std::string& ip, uint16_t port, UdpEndpoint& out) noexcept {
#ifdef _WIN32
    if (!WsaLifecycle::Acquire()) {
        return false;
    }
#endif
    in_addr addr{};
    const int res = inet_pton(AF_INET, ip.c_str(), &addr);
#ifdef _WIN32
    WsaLifecycle::Release();
#endif
    if (res != 1) {
        return false;
    }
    out.ipv4Net = addr.s_addr;
    out.portHost = port;
    return true;
}

UdpSocket::~UdpSocket() {
    Close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : handle_(other.handle_)
    , localPort_(other.localPort_)
    , bound_(other.bound_) {
    other.handle_ = kInvalidNativeHandle;
    other.localPort_ = 0;
    other.bound_ = false;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        Close();
        handle_ = other.handle_;
        localPort_ = other.localPort_;
        bound_ = other.bound_;
        other.handle_ = kInvalidNativeHandle;
        other.localPort_ = 0;
        other.bound_ = false;
    }
    return *this;
}

bool UdpSocket::Open(const UdpSocketOptions& options) {
    Close();

#ifdef _WIN32
    if (!WsaLifecycle::Acquire()) {
        return false;
    }
    SOCKET s = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
        WsaLifecycle::Release();
        return false;
    }
    handle_ = static_cast<NativeHandle>(s);
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        return false;
    }
    handle_ = s;
#endif

#ifdef _WIN32
    if (options.exclusiveAddressUse) {
        const int excl = 1;
        setsockopt(static_cast<SOCKET>(handle_), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&excl), sizeof(excl));
    } else
#endif
    {
        const int reuse = options.reuseAddress ? 1 : 0;
        setsockopt(
#ifdef _WIN32
            static_cast<SOCKET>(handle_),
#else
            handle_,
#endif
            SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)
        );
    }

    if (options.recvBufferBytes > 0) {
        const int rcv = static_cast<int>(options.recvBufferBytes);
        setsockopt(
#ifdef _WIN32
            static_cast<SOCKET>(handle_),
#else
            handle_,
#endif
            SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcv), sizeof(rcv)
        );
    }

    if (options.sendBufferBytes > 0) {
        const int snd = static_cast<int>(options.sendBufferBytes);
        setsockopt(
#ifdef _WIN32
            static_cast<SOCKET>(handle_),
#else
            handle_,
#endif
            SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&snd), sizeof(snd)
        );
    }

#ifdef _WIN32
    u_long mode = options.nonBlocking ? 1u : 0u;
    if (ioctlsocket(static_cast<SOCKET>(handle_), FIONBIO, &mode) != 0) {
        Close();
        return false;
    }
    // Disable Windows WSAECONNRESET on UDP sockets caused by ICMP port unreachable unless explicitly requested
    if (!options.reportConnReset) {
        BOOL newBehavior = FALSE;
        DWORD bytesReturned = 0;
        WSAIoctl(static_cast<SOCKET>(handle_), SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior),
                 nullptr, 0, &bytesReturned, nullptr, nullptr);
    }
#else
    int flags = fcntl(handle_, F_GETFL, 0);
    if (flags < 0) {
        Close();
        return false;
    }
    flags = options.nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(handle_, F_SETFL, flags) < 0) {
        Close();
        return false;
    }
#endif

    return true;
}

bool UdpSocket::Bind(uint32_t bindIpv4Net, uint16_t portHost) {
    if (!IsOpen()) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = bindIpv4Net; // INADDR_ANY is 0
    addr.sin_port = htons(portHost);

#ifdef _WIN32
    if (::bind(static_cast<SOCKET>(handle_), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }
    sockaddr_in actual{};
    int actualLen = sizeof(actual);
    if (getsockname(static_cast<SOCKET>(handle_), reinterpret_cast<sockaddr*>(&actual), &actualLen) == 0) {
        localPort_ = ntohs(actual.sin_port);
    }
#else
    if (::bind(handle_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        return false;
    }
    sockaddr_in actual{};
    socklen_t actualLen = sizeof(actual);
    if (getsockname(handle_, reinterpret_cast<sockaddr*>(&actual), &actualLen) == 0) {
        localPort_ = ntohs(actual.sin_port);
    }
#endif

    bound_ = true;
    return true;
}

void UdpSocket::Close() noexcept {
    if (handle_ != kInvalidNativeHandle) {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(handle_));
        WsaLifecycle::Release();
#else
        ::close(handle_);
#endif
        handle_ = kInvalidNativeHandle;
    }
    localPort_ = 0;
    bound_ = false;
}

bool UdpSocket::IsOpen() const noexcept {
    return handle_ != kInvalidNativeHandle;
}

bool UdpSocket::IsBound() const noexcept {
    return bound_;
}

uint16_t UdpSocket::LocalPort() const noexcept {
    return localPort_;
}

int UdpSocket::LastError() const noexcept {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

int UdpSocket::WaitReadable(int timeoutMs) const noexcept {
    if (!IsOpen()) {
        return -1;
    }
#ifdef _WIN32
    WSAPOLLFD pfd{};
    pfd.fd = static_cast<SOCKET>(handle_);
    pfd.events = POLLRDNORM;
    const int res = WSAPoll(&pfd, 1, timeoutMs);
    if (res > 0 && (pfd.revents & POLLERR)) {
        return -1;
    }
    return res;
#else
    pollfd pfd{};
    pfd.fd = handle_;
    pfd.events = POLLIN;
    int res = 0;
    do {
        res = ::poll(&pfd, 1, timeoutMs);
    } while (res < 0 && errno == EINTR);
    return res;
#endif
}

int UdpSocket::SendTo(const UdpEndpoint& target, const void* data, std::size_t size) const noexcept {
    if (!IsOpen() || !data || size == 0) {
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = target.ipv4Net;
    addr.sin_port = htons(target.portHost);

#ifdef _WIN32
    const int sent = ::sendto(
        static_cast<SOCKET>(handle_),
        static_cast<const char*>(data),
        static_cast<int>(size),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );
    if (sent == SOCKET_ERROR) {
        return IsWouldBlock(WSAGetLastError()) ? 0 : -1;
    }
    return sent;
#else
    const ssize_t sent = ::sendto(
        handle_,
        data,
        size,
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );
    if (sent < 0) {
        return IsWouldBlock(errno) ? 0 : -1;
    }
    return static_cast<int>(sent);
#endif
}

int UdpSocket::RecvFrom(void* buffer, std::size_t maxLen, UdpEndpoint& outSender) const noexcept {
    if (!IsOpen() || !buffer || maxLen == 0) {
        return -1;
    }

    sockaddr_in from{};
#ifdef _WIN32
    int fromLen = sizeof(from);
    const int received = ::recvfrom(
        static_cast<SOCKET>(handle_),
        static_cast<char*>(buffer),
        static_cast<int>(maxLen),
        0,
        reinterpret_cast<sockaddr*>(&from),
        &fromLen
    );
    if (received == SOCKET_ERROR) {
        return IsWouldBlock(WSAGetLastError()) ? 0 : -1;
    }
#else
    socklen_t fromLen = sizeof(from);
    const ssize_t received = ::recvfrom(
        handle_,
        buffer,
        maxLen,
        0,
        reinterpret_cast<sockaddr*>(&from),
        &fromLen
    );
    if (received < 0) {
        return IsWouldBlock(errno) ? 0 : -1;
    }
#endif

    outSender.ipv4Net = from.sin_addr.s_addr;
    outSender.portHost = ntohs(from.sin_port);
    return static_cast<int>(received);
}

int UdpSocket::RecvBatch(BatchItem* items, std::size_t maxItems) const noexcept {
    if (!IsOpen() || !items || maxItems == 0) {
        return -1;
    }

#if defined(__linux__)
    constexpr std::size_t kStackBatchCap = 64;
    const std::size_t count = std::min(maxItems, kStackBatchCap);
    mmsghdr msgs[kStackBatchCap]{};
    iovec iovecs[kStackBatchCap]{};
    sockaddr_in addrs[kStackBatchCap]{};

    for (std::size_t i = 0; i < count; ++i) {
        iovecs[i].iov_base = items[i].data;
        iovecs[i].iov_len = items[i].capacity;
        msgs[i].msg_hdr.msg_name = &addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }

    const int n = ::recvmmsg(handle_, msgs, static_cast<unsigned int>(count), MSG_DONTWAIT, nullptr);
    if (n < 0) {
        return IsWouldBlock(errno) ? 0 : -1;
    }

    for (int i = 0; i < n; ++i) {
        items[i].receivedSize = msgs[i].msg_len;
        items[i].sender.ipv4Net = addrs[i].sin_addr.s_addr;
        items[i].sender.portHost = ntohs(addrs[i].sin_port);
    }
    return n;
#else
    int total = 0;
    for (std::size_t i = 0; i < maxItems; ++i) {
        const int r = RecvFrom(items[i].data, items[i].capacity, items[i].sender);
        if (r < 0) {
            return total > 0 ? total : -1;
        }
        if (r == 0) {
            break;
        }
        items[i].receivedSize = static_cast<std::size_t>(r);
        ++total;
    }
    return total;
#endif
}

} // namespace svplus::net
