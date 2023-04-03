#pragma once

#include <functional>
#include <array>

typedef std::vector<u8>* PacketBuffer;
typedef std::vector<PacketBuffer> SendBuffer;

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
    static void ProcessCommand(const std::function<std::vector<u8> *(CommandBuffer &RecvBuffer, size_t RecvBufferLength)> &processor);
    static void SendMessage(const std::function<std::vector<u8> *()> &processor);
    static void ParseClientPacket();
    static void ParseHandshake();
    static void ParseRemoteLuaExec();
};
