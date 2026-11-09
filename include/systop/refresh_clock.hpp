#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>

namespace monitor {

    class RefreshClock {
    private:
        mutable std::mutex m_mutex;
        std::condition_variable_any m_changed;
        std::size_t m_index;
        std::uint64_t m_generation{ 0 };

    public:
        RefreshClock();

        [[nodiscard]] auto Interval() const -> std::chrono::milliseconds;
        auto Step(int delta) -> void;
        [[nodiscard]] auto WaitForTick(std::stop_token const& stop) -> bool;
    };

} // namespace monitor
