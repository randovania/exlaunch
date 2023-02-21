#pragma once

#include <functional>
#include <array>

// Client's interest. e.g. logging is only forwarded to client if it was set in handshake
struct ClientSubscriptions {
    bool logging;
    bool multiworldUpdates;
};

enum PacketType {
    PACKET_HANDSHAKE = 1,
    PACKET_LOG_MESSAGE,
    PACKET_REMOTE_LUA_EXEC,
    PACKET_KEEP_ALIVE
};

enum ClientInterests {
    LOGGING = 1,
    MULTIWORLD_UPDATE = 2,
    REMOTE_LUA_EXEC = 4
};

class RemoteApi {
  public:
    static constexpr const int VERSION = 1;
    static constexpr const int BufferSize = 4096;
    static struct ClientSubscriptions clientSubs;
    using CommandBuffer = std::array<char, BufferSize>;

    static void Init();
    static void ProcessCommand(const std::function<char*(CommandBuffer &RecvBuffer, size_t RecvBufferLength, size_t &size)> &processor);
    static void SendLog(const std::function<char*(size_t &size)> &processor);
    static void ParseClientPacket();
    static void ParseHandshake();
    static void ParseRemoteLuaExec();
};
