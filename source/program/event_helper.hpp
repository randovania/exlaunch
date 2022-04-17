#pragma once

#include <lib.hpp>
#include <nn.hpp>

namespace shimmer::util {

    class Event {
        private:
        nn::os::EventType m_Internal;

        public:
        void Initialize(bool signaled, nn::os::EventClearMode clearMode) {
            nn::os::InitializeEvent(&m_Internal, signaled, clearMode);
        }

        void Signal() {
            nn::os::SignalEvent(&m_Internal);
        }

        void Wait() {
            nn::os::WaitEvent(&m_Internal);
        }

        bool TryWait() {
            return nn::os::TryWaitEvent(&m_Internal);
        }

        //bool TimedWait(nn::os::Timespan timeout) {
        //    return nn::os::TimedWaitEvent(&m_Internal, timeout);
        //}

        void Clear() {
            nn::os::ClearEvent(&m_Internal);
        }

        ~Event() {
            nn::os::FinalizeEvent(&m_Internal);
        }
    };
}