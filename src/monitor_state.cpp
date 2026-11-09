#include "monitor_state.hpp"

#include <algorithm>
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

        auto name_less(std::string_view lhs, std::string_view rhs) -> bool {
            return std::ranges::lexicographical_compare(lhs, rhs, {}, to_lower_ascii, to_lower_ascii);
        }

        auto sort_processes(std::vector<ProcessUsage>& processes, SortKey key) -> void {
            switch (key) {
                case SortKey::Cpu:
                    std::ranges::sort(processes, [](ProcessUsage const& lhs, ProcessUsage const& rhs) {
                        return std::tie(rhs.cpu, rhs.memory, lhs.pid) < std::tie(lhs.cpu, lhs.memory, rhs.pid);
                    });
                    break;
                case SortKey::Memory:
                    std::ranges::sort(processes, [](ProcessUsage const& lhs, ProcessUsage const& rhs) {
                        return std::tie(rhs.memory, lhs.pid) < std::tie(lhs.memory, rhs.pid);
                    });
                    break;
                case SortKey::Name:
                    std::ranges::sort(processes, [](ProcessUsage const& lhs, ProcessUsage const& rhs) {
                        if (name_less(lhs.name, rhs.name)) {
                            return true;
                        }
                        if (name_less(rhs.name, lhs.name)) {
                            return false;
                        }
                        return lhs.pid < rhs.pid;
                    });
                    break;
                case SortKey::Pid:
                    std::ranges::sort(processes, {}, &ProcessUsage::pid);
                    break;
            }
        }

    } // namespace

    MonitorState::MonitorState(StaticInfo static_info) : info{ std::move(static_info) } {}

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

    auto MonitorState::ChangeInterval(int step) -> void {
        auto const last{ static_cast<int>(REFRESH_INTERVALS.size()) - 1 };
        auto const next{ std::clamp(static_cast<int>(interval_index.load()) + step, 0, last) };
        interval_index.store(static_cast<std::size_t>(next));
    }

    auto MonitorState::Interval() const -> std::chrono::milliseconds {
        return REFRESH_INTERVALS[interval_index.load()];
    }

} // namespace monitor
