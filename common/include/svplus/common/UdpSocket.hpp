/*
 * SampVoice Plus - Cross-Platform High-Throughput UDP Socket
 *
 * Uses WinSock2 (WSAPoll) on Windows and POSIX sockets (poll + recvmmsg batching) on Linux.
 * Eliminates the artificial 5ms sleep-polling present in legacy SampVoice.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace svplus::net {

struct UdpEndpoint {
    uint32_t ipv4Net{0};   // IPv4 address in network byte order (sin_addr.s_addr)
    uint16_t portHost{0};  // Port in host byte order

    bool operator==(const UdpEndpoint& other) const noexcept {
        return ipv4Net == other.ipv4Net && portHost == other.portHost;
    }

    bool operator!=(const UdpEndpoint& other) const noexcept {
        return !(*this == other);
    }

    static bool FromString(const std::string& ip, uint16_t port, UdpEndpoint& out) noexcept;
};

struct UdpSocketOptions {
    uint32_t recvBufferBytes{4 * 1024 * 1024};
    uint32_t sendBufferBytes{4 * 1024 * 1024};
    bool nonBlocking{true};
    bool reuseAddress{false};
    bool exclusiveAddressUse{false}; // Windows SO_EXCLUSIVEADDRUSE for fixed server ports
    bool reportConnReset{false};     // Preserve Windows WSAECONNRESET notifications when client monitors server UDP reachability
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    bool Open(const UdpSocketOptions& options = {});
    bool Bind(uint32_t bindIpv4Net = 0, uint16_t portHost = 0);
    void Close() noexcept;

    bool IsOpen() const noexcept;
    bool IsBound() const noexcept;
    uint16_t LocalPort() const noexcept;
    int LastError() const noexcept;

    /**
     * Blocks up to timeoutMs waiting for incoming datagrams.
     * Returns > 0 if readable, 0 on timeout, < 0 on error.
     */
    int WaitReadable(int timeoutMs) const noexcept;

    /**
     * Sends a datagram to the target endpoint.
     * Returns bytes sent, or -1 on error (check LastError() / EWOULDBLOCK).
     */
    int SendTo(const UdpEndpoint& target, const void* data, std::size_t size) const noexcept;

    /**
     * Receives a single datagram.
     * Returns bytes received, 0 if would-block / no data, or -1 on fatal error.
     */
    int RecvFrom(void* buffer, std::size_t maxLen, UdpEndpoint& outSender) const noexcept;

    struct BatchItem {
        void* data{nullptr};
        std::size_t capacity{0};
        std::size_t receivedSize{0};
        UdpEndpoint sender{};
    };

    /**
     * Receives up to items.size() datagrams in one batch (uses recvmmsg on Linux).
     * Returns number of datagrams populated in items, or -1 on fatal error.
     */
    int RecvBatch(BatchItem* items, std::size_t maxItems) const noexcept;

private:
#ifdef _WIN32
    using NativeHandle = std::uintptr_t;
    static constexpr NativeHandle kInvalidNativeHandle = static_cast<NativeHandle>(~0ull);
#else
    using NativeHandle = int;
    static constexpr NativeHandle kInvalidNativeHandle = -1;
#endif

    NativeHandle handle_{kInvalidNativeHandle};
    uint16_t localPort_{0};
    bool bound_{false};
};

} // namespace svplus::net
