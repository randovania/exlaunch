#pragma once

#include <functional>
#include <array>
#include "lua-5.1.5/src/lua.hpp"

typedef std::unique_ptr<std::vector<u8>> PacketBuffer;
using SendBuffer = std::vector<PacketBuffer>;

// Client's interest. e.g. logging is only forwarded to client if it was set in handshake
struct ClientSubscriptions {
    bool logging;
    bool multiWorld;
};

enum PacketType {
    PACKET_HANDSHAKE = 1,
    PACKET_LOG_MESSAGE,
    PACKET_REMOTE_LUA_EXEC,
    PACKET_KEEP_ALIVE,
    PACKET_NEW_INVENTORY,
    PACKET_COLLECTED_INDICES,
    PACKET_RECEIVED_PICKUPS,
    PACKET_GAME_STATE,
    PACKET_MALFORMED,
};

enum ClientInterests {
    LOGGING = 1,
    LOCATION_COLLECTED = 2
};

class RemoteApi {
  public:
    static constexpr const int VERSION = 1;
    static constexpr const int BufferSize = 4096;
    static struct ClientSubscriptions clientSubs;
    using CommandBuffer = std::array<char, BufferSize>;

    static void Init();
    static void ProcessCommand(lua_State* L, const std::function<PacketBuffer(lua_State* L, CommandBuffer &RecvBuffer, size_t RecvBufferLength)> &processor);
    static void SendMessage(lua_State* L, PacketType packetType, const std::function<PacketBuffer(lua_State* L, PacketType packetType)> &processor);
    static void ParseClientPacket();
    static void ParseHandshake();
    static void ParseRemoteLuaExec();
    static void SendMalformedPacket(PacketType packet_type, ssize_t receivedBytes, int should);
    static bool CheckReceivedBytes(PacketType packet_type, ssize_t receivedBytes, int should);
    static bool IsConnected();
};
