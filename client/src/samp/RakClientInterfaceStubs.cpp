/*
 * Out-of-line stub definitions for RakClientInterface's non-pure virtual methods.
 * ProxyRakClient overrides every virtual method and delegates to SA-MP's original instance;
 * these stubs only satisfy MSVC/GCC vtable generation for RakClientInterface without modifying third_party/rakhook.
 */

#include <RakNet/RakClientInterface.h>

bool RakClientInterface::Connect(const char*, unsigned short, unsigned short, unsigned int, int) { return false; }
void RakClientInterface::Disconnect(unsigned int, unsigned char) {}
void RakClientInterface::InitializeSecurity(const char*, const char*) {}
void RakClientInterface::SetPassword(const char*) {}
bool RakClientInterface::HasPassword(void) const { return false; }
bool RakClientInterface::Send(const char*, const int, PacketPriority, PacketReliability, char) { return false; }
bool RakClientInterface::Send(RakNet::BitStream*, PacketPriority, PacketReliability, char) { return false; }
Packet* RakClientInterface::Receive(void) { return nullptr; }
void RakClientInterface::DeallocatePacket(Packet*) {}
void RakClientInterface::PingServer(void) {}
void RakClientInterface::PingServer(const char*, unsigned short, unsigned short, bool) {}
int RakClientInterface::GetAveragePing(void) { return 0; }
int RakClientInterface::GetLastPing(void) const { return 0; }
int RakClientInterface::GetLowestPing(void) const { return 0; }
int RakClientInterface::GetPlayerPing(const PlayerID) { return 0; }
void RakClientInterface::StartOccasionalPing(void) {}
void RakClientInterface::StopOccasionalPing(void) {}
bool RakClientInterface::IsConnected(void) const { return false; }
unsigned int RakClientInterface::GetSynchronizedRandomInteger(void) const { return 0; }
bool RakClientInterface::GenerateCompressionLayer(unsigned int[256], bool) { return false; }
bool RakClientInterface::DeleteCompressionLayer(bool) { return false; }
void RakClientInterface::RegisterAsRemoteProcedureCall(int*, void (*)(RPCParameters*)) {}
void RakClientInterface::RegisterClassMemberRPC(int*, void*) {}
void RakClientInterface::UnregisterAsRemoteProcedureCall(int*) {}
bool RakClientInterface::RPC(int*, const char*, unsigned int, PacketPriority, PacketReliability, char, bool) { return false; }
bool RakClientInterface::RPC(int*, RakNet::BitStream*, PacketPriority, PacketReliability, char, bool) { return false; }
bool RakClientInterface::RPC_(int*, RakNet::BitStream*, PacketPriority, PacketReliability, char, bool, NetworkID) { return false; }
void RakClientInterface::SetTrackFrequencyTable(bool) {}
bool RakClientInterface::GetSendFrequencyTable(unsigned int[256]) { return false; }
float RakClientInterface::GetCompressionRatio(void) const { return 0.0f; }
float RakClientInterface::GetDecompressionRatio(void) const { return 0.0f; }
void RakClientInterface::AttachPlugin(void*) {}
void RakClientInterface::DetachPlugin(void*) {}
RakNet::BitStream* RakClientInterface::GetStaticServerData(void) { return nullptr; }
void RakClientInterface::SetStaticServerData(const char*, const int) {}
RakNet::BitStream* RakClientInterface::GetStaticClientData(const PlayerID) { return nullptr; }
void RakClientInterface::SetStaticClientData(const PlayerID, const char*, const int) {}
void RakClientInterface::SendStaticClientDataToServer(void) {}
PlayerID RakClientInterface::GetServerID(void) const { return UNASSIGNED_PLAYER_ID; }
PlayerID RakClientInterface::GetPlayerID(void) const { return UNASSIGNED_PLAYER_ID; }
PlayerID RakClientInterface::GetInternalID(void) const { return UNASSIGNED_PLAYER_ID; }
const char* RakClientInterface::PlayerIDToDottedIP(const PlayerID) const { return ""; }
void RakClientInterface::PushBackPacket(Packet*, bool) {}
void RakClientInterface::SetRouterInterface(void*) {}
void RakClientInterface::RemoveRouterInterface(void*) {}
void RakClientInterface::SetTimeoutTime(RakNetTime) {}
bool RakClientInterface::SetMTUSize(int) { return false; }
int RakClientInterface::GetMTUSize(void) const { return 0; }
void RakClientInterface::AllowConnectionResponseIPMigration(bool) {}
void RakClientInterface::AdvertiseSystem(const char*, unsigned short, const char*, int) {}
void* const RakClientInterface::GetStatistics(void) { return nullptr; }
void RakClientInterface::ApplyNetworkSimulator(double, unsigned short, unsigned short) {}
bool RakClientInterface::IsNetworkSimulatorActive(void) { return false; }
PlayerIndex RakClientInterface::GetPlayerIndex(void) { return 0; }
