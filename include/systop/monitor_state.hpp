#pragma once

#include <cstdint>
#include <optional>

#include "systop/history.hpp"
#include "systop/sampler.hpp"

namespace monitor {

    enum class SortKey {
        Cpu,
        Memory,
        Name,
        Pid,
    };

    [[nodiscard]] constexpr auto is_descending(SortKey key) -> bool {
        return key == SortKey::Cpu || key == SortKey::Memory;
    }

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

        History cpu_history;
        History memory_history;
        History net_receive_history;
        History net_send_history;
        History disk_read_history;
        History disk_write_history;

        SortKey sort_key{ SortKey::Cpu };
        int selected{ 0 };
        std::optional<std::uint32_t> followed_pid;

        auto Apply(Snapshot snapshot) -> void;
        auto SortBy(SortKey key) -> void;
        auto Select(int index) -> void;
    };

} // namespace monitor
