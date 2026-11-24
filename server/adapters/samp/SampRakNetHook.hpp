/*
 * SampVoice Plus - Legacy SA-MP 0.3.7 RakServer VTable & Connect Hook
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "SampStructs.hpp"

namespace svplus::server::samp {

class SampRakNetHook {
public:
    using ConnectCallback    = std::function<void(uint16_t playerId, const uint8_t* rpcBytes, std::size_t size)>;
    using DisconnectCallback = std::function<void(uint16_t playerId)>;
    using PacketCallback     = std::function<bool(uint16_t playerId, const uint8_t* data, std::size_t size)>;

    static bool Initialize(const void* anchorInServerModule);
    static void AttachRakServerIfUnbound(void* rakServer);
    static void Shutdown() noexcept;
    static bool Is03DL() noexcept;

    static void SetCallbacks(ConnectCallback onConnect, DisconnectCallback onDisconnect, PacketCallback onPacket);

    static bool SendPacket(uint16_t playerId, const uint8_t* bytes, std::size_t size) noexcept;
    static uint32_t GetPlayerIpv4Net(uint16_t playerId) noexcept;

private:
    static void BindRakServer(void* rakServer) noexcept;

    static void* GetRakServerInterfaceHook();
    static void ConnectRpcHook(RakRpcParameters* rpc);

#ifdef _WIN32
    static void* __fastcall DestructorHook(void* _this, void* edx, unsigned int flags);
    static void __fastcall RegisterRpcHook(void* _this, void* edx, uint8_t* rpcIdPtr, void(*handler)(RakRpcParameters*));
    static RakPacket* __fastcall ReceiveHook(void* _this, void* edx);
#else
    static void DestructorHook(void* _this);
    static void RegisterRpcHook(void* _this, uint8_t* rpcIdPtr, void(*handler)(RakRpcParameters*));
    static RakPacket* ReceiveHook(void* _this);
#endif
};

} // namespace svplus::server::samp
