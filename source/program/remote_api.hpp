#pragma once

#include <functional>
#include <array>

class RemoteApi {
    public:
    static constexpr const int VERSION = 1;
    static constexpr const int BufferSize = 4096;
    using CommandBuffer = std::array<char, BufferSize>;

    static void Init();
    static void ProcessCommand(const std::function<size_t (CommandBuffer& buffer, size_t bufferLength)>& processor);
};
    