#include "systop/ui/monitor_view.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include "systop/core/monitor_state.hpp"
#include "systop/core/refresh_clock.hpp"

using namespace ftxui;

namespace monitor {

    namespace {

        constexpr double RATE_SCALE_FLOOR{ 64.0 * 1024.0 };
        constexpr std::size_t CORE_ROWS{ 4 };
        constexpr std::size_t MAX_CORE_COLUMNS{ 4 };
        constexpr int WHEEL_STEP{ 3 };

        constexpr int PID_WIDTH{ 8 };
        constexpr int CPU_WIDTH{ 8 };
        constexpr int MEMORY_WIDTH{ 12 };
        constexpr int THREAD_WIDTH{ 7 };
        constexpr int HANDLE_WIDTH{ 8 };

        constexpr std::array<std::pair<char, SortKey>, 4> SORT_HOTKEYS{
            {
             { 'c', SortKey::Cpu },
             { 'm', SortKey::Memory },
             { 'n', SortKey::Name },
             { 'i', SortKey::Pid },
             }
        };

        class ElasticHeight : public Node {
        public:
            explicit ElasticHeight(Element child) : Node{ Elements{ std::move(child) } } {}

            auto ComputeRequirement() -> void override {
                Node::ComputeRequirement();
                requirement_.min_y = 0;
                requirement_.flex_grow_y = 1;
                requirement_.flex_shrink_y = 1;
            }

            auto SetBox(Box box) -> void override {
                Node::SetBox(box);
                children_.front()->SetBox(box);
            }
        };

        auto elastic_height(Element child) -> Element {
            return std::make_shared<ElasticHeight>(std::move(child));
        }

        auto format_bytes(double bytes) -> std::string {
            constexpr std::array<std::string_view, 5> UNITS{ "B", "KiB", "MiB", "GiB", "TiB" };
            std::size_t unit{ 0 };
            while (bytes >= 1024.0 && unit + 1 < UNITS.size()) {
                bytes /= 1024.0;
                ++unit;
            }
            if (unit == 0) {
                return std::format("{:.0f} {}", bytes, UNITS[unit]);
            }
            return std::format("{:.1f} {}", bytes, UNITS[unit]);
        }

        auto format_rate(double bytes_per_second) -> std::string {
            return std::format("{}/s", format_bytes(bytes_per_second));
        }

        auto format_uptime(std::chrono::milliseconds uptime) -> std::string {
            auto const days{ std::chrono::floor<std::chrono::days>(uptime) };
            auto const clock{ std::chrono::floor<std::chrono::seconds>(uptime - days) };
            if (days.count() > 0) {
                return std::format("{} 天 {:%T}", days.count(), clock);
            }
            return std::format("{:%T}", clock);
        }

        auto usage_color(double percent) -> Color {
            if (percent >= 85.0) {
                return Color::Red;
            }
            if (percent >= 60.0) {
                return Color::Yellow;
            }
            return Color::Green;
        }

        auto meter(double percent) -> Element {
            auto const progress{ static_cast<float>(std::clamp(percent, 0.0, 100.0) / 100.0) };
            return gauge(progress) | color(usage_color(percent)) | bgcolor(Color::Grey23);
        }

        auto collecting() -> Element {
            return text("正在采集…") | dim;
        }

        auto history_graph(History const& history, double scale_floor) -> Element {
            return elastic_height(graph([&history, scale_floor](int width, int height) {
                auto const& values{ history.Values() };
                auto const count{ std::min(values.size(), static_cast<std::size_t>(width)) };
                std::ranges::subrange const visible{ values.end() - static_cast<std::ptrdiff_t>(count), values.end() };
                auto const scale{ std::ranges::fold_left(visible, scale_floor, [](double lhs, double rhs) { return std::max(lhs, rhs); }) };

                std::vector<int> output(static_cast<std::size_t>(width), -1);
                std::ranges::transform(visible, output.end() - static_cast<std::ptrdiff_t>(count), [scale, height](double value) {
                    return static_cast<int>(std::lround(std::clamp(value / scale, 0.0, 1.0) * height)) - 1;
                });
                return output;
            }));
        }

        auto title(std::string_view name) -> Element {
            return text(std::format(" {} ", name)) | bold;
        }

        auto title(std::string_view name, Element detail) -> Element {
            return hbox({ title(name), std::move(detail), text(" ") });
        }

        auto percent_text(double percent) -> Element {
            return text(std::format("{:.1f}%", percent)) | color(usage_color(percent));
        }

        auto right_column(Element content, int width) -> Element {
            return align_right(std::move(content)) | size(WIDTH, EQUAL, width);
        }

        auto core_cell(std::size_t index, double load) -> Element {
            return hbox({
                       text(std::format("{:>3} ", index)) | dim,
                       meter(load),
                       text(std::format("{:>4.0f}%", load)),
                   }) |
                flex;
        }

        auto core_grid(std::vector<double> const& loads) -> Element {
            if (loads.empty()) {
                return collecting();
            }
            auto const columns{ std::min((loads.size() + CORE_ROWS - 1) / CORE_ROWS, MAX_CORE_COLUMNS) };
            std::vector<Elements> rows;
            for (std::size_t index{ 0 }; index < loads.size(); ++index) {
                if (index % columns == 0) {
                    rows.emplace_back();
                } else {
                    rows.back().push_back(text("  "));
                }
                rows.back().push_back(core_cell(index, loads[index]));
            }
            return gridbox(std::move(rows));
        }

        auto cpu_panel(MonitorState const& state) -> Element {
            auto const& snapshot{ state.latest };
            auto const& info{ state.info };
            auto content{ vbox({
                hbox({
                    text(info.cpu_name) | dim | flex,
                    text(std::format(" {:.2f} GHz · {} 核 {} 线程", snapshot.cpu_mhz / 1000.0, info.physical_cores, info.logical_cores)),
                }),
                history_graph(state.cpu_history, 100.0) | color(Color::Cyan),
                separator(),
                core_grid(snapshot.cpu_cores),
            }) };
            return window(title("CPU", percent_text(snapshot.cpu_total)), std::move(content));
        }

        auto usage_row(std::string_view label, double percent, std::string detail) -> Element {
            return hbox({
                text(label) | size(WIDTH, EQUAL, 5),
                meter(percent),
                text(std::format(" {:>5.1f}%  ", percent)),
                text(std::move(detail)) | dim,
            });
        }

        auto byte_usage_row(std::string_view label, std::uint64_t used, std::uint64_t total) -> Element {
            return usage_row(label, percent_of(used, total), std::format("{} / {}", format_bytes(used), format_bytes(total)));
        }

        auto memory_panel(MonitorState const& state) -> Element {
            auto const& snapshot{ state.latest };
            auto content{ vbox({
                history_graph(state.memory_history, 100.0) | color(Color::Magenta),
                separator(),
                byte_usage_row("物理", snapshot.memory_used, snapshot.memory_total),
                byte_usage_row("提交", snapshot.commit_used, snapshot.commit_limit),
                hbox({
                    text("可用 ") | dim,
                    text(format_bytes(snapshot.memory_total - snapshot.memory_used)),
                    text("   缓存 ") | dim,
                    text(format_bytes(snapshot.system_cache)),
                }),
            }) };
            return window(title("内存", percent_text(percent_of(snapshot.memory_used, snapshot.memory_total))), std::move(content));
        }

        struct RateSeries {
            std::string_view label;
            double value;
            History const& history;
            Color tint;
        };

        auto rate_series(RateSeries const& series) -> Element {
            return vbox({
                       hbox({ text(series.label) | color(series.tint), filler(), text(format_rate(series.value)) | bold }),
                       history_graph(series.history, RATE_SCALE_FLOOR) | color(series.tint),
                   }) |
                flex;
        }

        auto rate_panel(Element panel_title, RateSeries const& upper, RateSeries const& lower) -> Element {
            return window(std::move(panel_title), vbox({ rate_series(upper), rate_series(lower) }));
        }

        auto network_panel(MonitorState const& state) -> Element {
            return rate_panel(title("网络"), { "↓ 接收", state.latest.net_receive, state.net_receive_history, Color::Green }, { "↑ 发送", state.latest.net_send, state.net_send_history, Color::Red });
        }

        auto disk_io_panel(MonitorState const& state) -> Element {
            auto const active{ state.latest.disk_active };
            auto detail{ text(std::format("活动 {:.0f}%", active)) | color(usage_color(active)) };
            return rate_panel(title("磁盘 I/O", std::move(detail)), { "读取", state.latest.disk_read, state.disk_read_history, Color::Cyan }, { "写入", state.latest.disk_write, state.disk_write_history, Color::Yellow });
        }

        auto drives_panel(MonitorState const& state) -> Element {
            Elements rows;
            for (auto const& drive : state.latest.drives) {
                rows.push_back(usage_row(drive.root, percent_of(drive.total - drive.free, drive.total), std::format("剩 {}", format_bytes(drive.free))));
            }
            if (rows.empty()) {
                rows.push_back(collecting());
            }
            return window(title("磁盘空间"), vbox(std::move(rows)) | yframe);
        }

        auto column_label(std::string_view label, SortKey key, SortKey active) -> Element {
            if (key != active) {
                return text(label);
            }
            return text(std::format("{}{}", label, is_descending(key) ? "▼" : "▲")) | color(Color::Cyan);
        }

        auto reserve_scrollbar_column(Element content) -> Element {
            return vscroll_indicator(std::move(content));
        }

        auto process_columns(
            Element pid,
            Element name,
            Element cpu,
            Element memory,
            Element threads,
            Element handles
        ) -> Element {
            return hbox({
                right_column(std::move(pid), PID_WIDTH),
                text("  "),
                std::move(name) | flex,
                right_column(std::move(cpu), CPU_WIDTH),
                right_column(std::move(memory), MEMORY_WIDTH),
                right_column(std::move(threads), THREAD_WIDTH),
                right_column(std::move(handles), HANDLE_WIDTH),
                text(" "),
            });
        }

        auto process_header(SortKey active) -> Element {
            auto columns{ process_columns(column_label("PID", SortKey::Pid, active), column_label("名称", SortKey::Name, active), column_label("CPU%", SortKey::Cpu, active), column_label("内存", SortKey::Memory, active), text("线程"), text("句柄")) };
            return reserve_scrollbar_column(std::move(columns)) | bold | bgcolor(Color::Grey23);
        }

        auto process_row(ProcessUsage const& process) -> Element {
            auto cpu{ text(std::format("{:.1f}", process.cpu)) };
            if (process.cpu < 0.05) {
                cpu = std::move(cpu) | dim;
            }
            return process_columns(text(std::to_string(process.pid)), text(process.name), std::move(cpu), text(format_bytes(process.memory)), text(std::to_string(process.threads)), text(std::to_string(process.handles)));
        }

        auto process_rows(MonitorState const& state) -> Element {
            auto const& processes{ state.latest.processes };
            auto const count{ static_cast<int>(processes.size()) };
            auto const reach{ Terminal::Size().dimy };
            auto const first{ std::max(0, state.selected - reach) };
            auto const last{ std::min(count, state.selected + reach + 1) };

            Elements rows;
            rows.push_back(emptyElement() | size(HEIGHT, EQUAL, first));
            for (auto index{ first }; index < last; ++index) {
                auto row{ process_row(processes[static_cast<std::size_t>(index)]) };
                if (index == state.selected) {
                    row = std::move(row) | inverted | focus;
                }
                rows.push_back(std::move(row));
            }
            rows.push_back(emptyElement() | size(HEIGHT, EQUAL, count - last));
            return vbox(std::move(rows));
        }

        auto process_panel(MonitorState const& state, Box& list_box) -> Element {
            auto const& snapshot{ state.latest };
            auto summary{ text(std::format("共 {} · 线程 {} · 句柄 {}", snapshot.process_count, snapshot.thread_count, snapshot.handle_count)) | dim };
            auto content{ vbox({
                process_header(state.sort_key),
                elastic_height(process_rows(state) | vscroll_indicator | yframe) | reflect(list_box),
            }) };
            return window(title("进程", std::move(summary)), std::move(content));
        }

        auto header_bar(MonitorState const& state, std::chrono::time_zone const* zone) -> Element {
            std::chrono::zoned_time const now{ zone, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) };
            return hbox({
                text(" 系统资源监控 ") | bold | color(Color::Cyan),
                text("│") | dim,
                text(std::format(" {} ", state.info.host_name)) | bold,
                text(state.info.os_name) | dim | xflex_shrink,
                text(std::format("   运行 {}", format_uptime(state.latest.uptime))),
                filler(),
                text(std::format(" {:%H:%M:%S} ", now)),
            });
        }

        auto footer_bar(MonitorState const& state, RefreshClock const& clock) -> Element {
            return hbox({
                text(" ↑↓ PgUp PgDn Home End 选择 · c/m/n/i 按 CPU/内存/名称/PID 排序 · 空格 暂停 · +/- 刷新间隔 · q 退出") | dim | xflex_shrink,
                filler(),
                state.paused ? text(" 已暂停 ") | bold | color(Color::Black) | bgcolor(Color::Yellow) : emptyElement(),
                text(std::format(" 刷新 {} ", std::chrono::duration<double>{ clock.Interval() })),
            });
        }

        auto handle_navigation(MonitorState& state, Box const& list_box, Event& event) -> bool {
            auto const page{ std::max(1, list_box.y_max - list_box.y_min) };
            if (event == Event::ArrowUp || event == Event::Character('k')) {
                state.Select(state.selected - 1);
            } else if (event == Event::ArrowDown || event == Event::Character('j')) {
                state.Select(state.selected + 1);
            } else if (event == Event::PageUp) {
                state.Select(state.selected - page);
            } else if (event == Event::PageDown) {
                state.Select(state.selected + page);
            } else if (event == Event::Home) {
                state.Select(0);
            } else if (event == Event::End) {
                state.Select(static_cast<int>(state.latest.processes.size()) - 1);
            } else if (event.is_mouse() && event.mouse().button == Mouse::WheelUp) {
                state.Select(state.selected - WHEEL_STEP);
            } else if (event.is_mouse() && event.mouse().button == Mouse::WheelDown) {
                state.Select(state.selected + WHEEL_STEP);
            } else {
                return false;
            }
            return true;
        }

    } // namespace

    auto make_monitor(MonitorState& state, RefreshClock& clock, Closure quit) -> Component {
        auto list_box{ std::make_shared<Box>() };
        auto const* const zone{ std::chrono::current_zone() };

        auto view{ Renderer([&state, &clock, list_box, zone] {
            return vbox({
                header_bar(state, zone),
                hbox({ cpu_panel(state) | xflex_factor(3, 1), memory_panel(state) | xflex_factor(2, 1) }) | flex,
                hbox({ network_panel(state) | flex, disk_io_panel(state) | flex, drives_panel(state) | flex }) | flex,
                process_panel(state, *list_box) | yflex_factor(2, 1),
                footer_bar(state, clock),
            });
        }) };

        return CatchEvent(view, [&state, &clock, list_box, quit = std::move(quit)](Event event) {
            if (event == Event::Escape || event == Event::Character('q')) {
                quit();
                return true;
            }
            if (event == Event::Character(' ')) {
                state.paused = !state.paused;
                return true;
            }
            if (event == Event::Character('+') || event == Event::Character('=')) {
                clock.Step(1);
                return true;
            }
            if (event == Event::Character('-')) {
                clock.Step(-1);
                return true;
            }
            for (auto const& [hotkey, key] : SORT_HOTKEYS) {
                if (event == Event::Character(hotkey)) {
                    state.SortBy(key);
                    return true;
                }
            }
            return handle_navigation(state, *list_box, event);
        });
    }

} // namespace monitor
