#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "history.hpp"
#include "sampler.hpp"

namespace monitor {

    enum class SortKey {
        Cpu,
        Memory,
        Name,
        Pid,
    };

    inline constexpr std::size_t HISTORY_CAPACITY{ 1024 };

    inline constexpr std::array<std::chrono::milliseconds, 4> REFRESH_INTERVALS{
        std::chrono::milliseconds{ 500 },
        std::chrono::milliseconds{ 1000 },
        std::chrono::milliseconds{ 2000 },
        std::chrono::milliseconds{ 5000 },
    };

    [[nodiscard]] inline auto percent_of(std::uint64_t part, std::uint64_t whole) -> double {
        if (whole == 0) {
            return 0.0;
        }
        return static_cast<double>(part) * 100.0 / static_cast<double>(whole);
    }

    struct MonitorState {
        StaticInfo info;
        Snapshot latest;
        bool paused{ false };

        History cpu_history{ HISTORY_CAPACITY };
        History memory_history{ HISTORY_CAPACITY };
        History net_receive_history{ HISTORY_CAPACITY };
        History net_send_history{ HISTORY_CAPACITY };
        History disk_read_history{ HISTORY_CAPACITY };
        History disk_write_history{ HISTORY_CAPACITY };

        SortKey sort_key{ SortKey::Cpu };
        int selected{ 0 };
        std::optional<std::uint32_t> followed_pid;

        std::atomic<std::size_t> interval_index{ 1 };

        explicit MonitorState(StaticInfo static_info);

        auto Apply(Snapshot snapshot) -> void;
        auto SortBy(SortKey key) -> void;
        auto Select(int index) -> void;
        auto ChangeInterval(int step) -> void;
        [[nodiscard]] auto Interval() const -> std::chrono::milliseconds;
    };

} // namespace monitor
