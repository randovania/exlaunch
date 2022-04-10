#pragma once

#include <functional>

class RemoteApi {
    public:
    static void Init();
    static void ProcessCommand(const std::function<size_t (std::array<char, 4096>& buffer, size_t bufferLength)>& processor);
};
    