#include <lib.hpp>

#include "remote_api.hpp"

#include "static_thread.hpp"
#include "event_helper.hpp"

#include <nn.hpp>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <atomic>

namespace {
    static s32 g_TcpSocket = -1;
    static std::atomic<bool> readyForGameThread = false;
    static shimmer::util::Event commandParsedEvent;

    constexpr inline auto DefaultTcpAutoBufferSizeMax      = 192 * 1024; /* 192kb */
    constexpr inline auto MinTransferMemorySize            = (2 * DefaultTcpAutoBufferSizeMax + (128 * 1024));
    constexpr inline auto MinSocketAllocatorSize           = 128 * 1024;

    constexpr inline auto SocketAllocatorSize = MinSocketAllocatorSize * 1;
    constexpr inline auto TransferMemorySize = MinTransferMemorySize * 1;

    constexpr inline auto SocketPoolSize = SocketAllocatorSize + TransferMemorySize;
    constexpr inline auto SocketPort = 6969;

    static shimmer::util::StaticThread<0x4000> SocketSpawnThread;
    static RemoteApi::CommandBuffer sharedBuffer;
    static size_t bufferLength = 0;
};

void PrepareThread() {

    void* pool = aligned_alloc(0x4000, SocketPoolSize);
    R_ABORT_UNLESS(nn::socket::Initialize(pool, SocketPoolSize, SocketAllocatorSize, 14));

    commandParsedEvent.Initialize(false, nn::os::EventClearMode::EventClearMode_AutoClear);

    /* Open socket. */
    g_TcpSocket = nn::socket::Socket(AF_INET, SOCK_STREAM, 0);
    if(g_TcpSocket < 0) EXL_ABORT(69);

    /* Set socket to keep-alive. */
    // int flags = true;
    // nn::socket::SetSockOpt(g_TcpSocket, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));

    /* Open and wait for connection. */
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = nn::socket::InetHtons(SocketPort);

    int rval = nn::socket::Bind(g_TcpSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (rval < 0) EXL_ABORT(69);
    
    rval = nn::socket::Listen(g_TcpSocket, 1);
    if (rval < 0) EXL_ABORT(69);
}

void SocketSpawn(void*) {
    SocketSpawnThread.SetName("SocketSpawnThread");
    PrepareThread();

    bool looping = true;
    while (looping) {
        struct sockaddr clientAddr;
        u32 addrLen;
        s32 clientSocket = nn::socket::Accept(g_TcpSocket, &clientAddr, &addrLen);
        u8 requestNumber = 0;

        while (true) {
            ssize_t length = nn::socket::Recv(clientSocket, sharedBuffer.data(), sharedBuffer.size(), 0);
            if (length > 0) {
                bufferLength = length;
                readyForGameThread.store(true);
                commandParsedEvent.Wait();
                nn::socket::Send(clientSocket, &requestNumber, sizeof(u8), 0);
                nn::socket::Send(clientSocket, sharedBuffer.data(), bufferLength, 0);
                ++requestNumber;
            } else {
                break;
            }
        }
        nn::socket::Close(clientSocket);
    }

    nn::socket::Close(g_TcpSocket);
    svcExitThread();
}

void RemoteApi::Init() {
    R_ABORT_UNLESS(SocketSpawnThread.Create(SocketSpawn));
    SocketSpawnThread.Start();
    // /* Inject hook. */
}

void RemoteApi::ProcessCommand(const std::function<size_t (CommandBuffer& buffer, size_t bufferLength)>& processor) {
    if (readyForGameThread.load()) {
        bufferLength = processor(sharedBuffer, bufferLength);
        readyForGameThread.store(false);
        commandParsedEvent.Signal();
    }
}