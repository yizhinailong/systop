#include "systop/monitor_state.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace monitor {

    namespace {

        auto to_lower_ascii(char c) -> char {
            if (c >= 'A' && c <= 'Z') {
                return static_cast<char>(c - 'A' + 'a');
            }
            return c;
        }

        auto compare_names(std::string_view lhs, std::string_view rhs) -> std::strong_ordering {
            return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char a, char b) {
                return to_lower_ascii(a) <=> to_lower_ascii(b);
            });
        }

        auto compare_by(SortKey key, ProcessUsage const& lhs, ProcessUsage const& rhs) -> std::partial_ordering {
            switch (key) {
                case SortKey::Cpu:
                    return std::tie(lhs.cpu, lhs.memory) <=> std::tie(rhs.cpu, rhs.memory);
                case SortKey::Memory:
                    return lhs.memory <=> rhs.memory;
                case SortKey::Name:
                    return compare_names(lhs.name, rhs.name);
                case SortKey::Pid:
                    return lhs.pid <=> rhs.pid;
            }
            std::unreachable();
        }

        auto sort_processes(std::vector<ProcessUsage>& processes, SortKey key) -> void {
            std::ranges::sort(processes, [key](ProcessUsage const& lhs, ProcessUsage const& rhs) {
                auto const order{ is_descending(key) ? compare_by(key, rhs, lhs) : compare_by(key, lhs, rhs) };
                if (order != 0) {
                    return order < 0;
                }
                return lhs.pid < rhs.pid;
            });
        }

    } // namespace

    auto MonitorState::Apply(Snapshot snapshot) -> void {
        if (paused) {
            return;
        }
        latest = std::move(snapshot);
        cpu_history.Push(latest.cpu_total);
        memory_history.Push(percent_of(latest.memory_used, latest.memory_total));
        net_receive_history.Push(latest.net_receive);
        net_send_history.Push(latest.net_send);
        disk_read_history.Push(latest.disk_read);
        disk_write_history.Push(latest.disk_write);

        sort_processes(latest.processes, sort_key);
        if (followed_pid) {
            auto const found{ std::ranges::find(latest.processes, *followed_pid, &ProcessUsage::pid) };
            if (found == latest.processes.end()) {
                followed_pid.reset();
            } else {
                selected = static_cast<int>(found - latest.processes.begin());
            }
        }
        selected = std::clamp(selected, 0, std::max(0, static_cast<int>(latest.processes.size()) - 1));
    }

    auto MonitorState::SortBy(SortKey key) -> void {
        sort_key = key;
        sort_processes(latest.processes, sort_key);
        selected = 0;
        followed_pid.reset();
    }

    auto MonitorState::Select(int index) -> void {
        if (latest.processes.empty()) {
            return;
        }
        selected = std::clamp(index, 0, static_cast<int>(latest.processes.size()) - 1);
        followed_pid = latest.processes[static_cast<std::size_t>(selected)].pid;
    }

} // namespace monitor
