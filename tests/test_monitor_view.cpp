#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

import std;
import mcpplibs.mtest;
import systop;

namespace {

    using namespace std::chrono_literals;

    using ftxui::Event;
    using mcpplibs::mtest::UTest;
    using mcpplibs::mtest::contains;
    using mcpplibs::mtest::eq;
    using mcpplibs::mtest::expect;
    using mcpplibs::mtest::operator""_test;
    using monitor::SortKey;

    constexpr int SCREEN_WIDTH{ 160 };
    constexpr int SCREEN_HEIGHT{ 50 };
    constexpr int WHEEL_STEP{ 3 };
    constexpr std::array<std::string_view, 6> PROCESS_NAMES{ "alpha.exe", "beta.exe", "gamma.exe", "delta.exe", "epsilon.exe", "zeta.exe" };
    constexpr int LAST_ROW{ static_cast<int>(PROCESS_NAMES.size()) - 1 };

    constexpr std::array<std::pair<char, SortKey>, 4> SORT_HOTKEYS{
        {
         { 'm', SortKey::Memory },
         { 'n', SortKey::Name },
         { 'i', SortKey::Pid },
         { 'c', SortKey::Cpu },
         }
    };

    auto sample_snapshot() -> monitor::Snapshot {
        monitor::Snapshot snapshot{ .cpu_total = 37.5, .cpu_cores = { 10.0, 20.0, 50.0, 70.0 }, .memory_total = 8000, .memory_used = 2000 };
        for (std::uint32_t pid{ 1 }; auto const name : PROCESS_NAMES) {
            snapshot.processes.push_back({ .pid = pid, .name = std::string{ name }, .cpu = static_cast<double>(pid), .memory = pid * 100 });
            ++pid;
        }
        return snapshot;
    }

    auto wheel(ftxui::Mouse::Button button) -> Event {
        return Event::Mouse("", ftxui::Mouse{ .button = button });
    }

    struct Fixture {
        monitor::MonitorState state{ .info = { .host_name = "test-host", .os_name = "Windows Test", .cpu_name = "Test CPU", .physical_cores = 2, .logical_cores = 4 } };
        monitor::RefreshClock clock;
        int quit_count{ 0 };
        ftxui::Component root{ monitor::make_monitor(state, clock, [this] { ++quit_count; }) };

        Fixture() {
            state.Apply(sample_snapshot());
        }

        auto Render() -> std::string {
            auto screen{ ftxui::Screen::Create(ftxui::Dimension::Fixed(SCREEN_WIDTH), ftxui::Dimension::Fixed(SCREEN_HEIGHT)) };
            ftxui::Render(screen, root->Render());
            return screen.ToString();
        }

        auto Press(Event const& event) -> bool {
            return root->OnEvent(event);
        }
    };

    UTest rendering{ "render", [] {
        Fixture fixture;
        auto const output{ fixture.Render() };
        "shows the host and CPU names"_test = [&] {
            expect(contains(output, "test-host"));
            expect(contains(output, "Test CPU"));
        };
        "shows every process"_test = [&] {
            for (auto const name : PROCESS_NAMES) {
                expect(contains(output, name));
            }
        };
    } };

    UTest keys{ "keys", [] {
        "space toggles pausing"_test = [] {
            Fixture fixture;
            expect(fixture.Press(Event::Character(' ')));
            expect(fixture.state.paused);
            expect(fixture.Press(Event::Character(' ')));
            expect(!fixture.state.paused);
        };
        "plus, equals and minus step the refresh interval"_test = [] {
            Fixture fixture;
            expect(fixture.Press(Event::Character('+')));
            expect(eq(fixture.clock.Interval(), 2000ms));
            expect(fixture.Press(Event::Character('=')));
            expect(eq(fixture.clock.Interval(), 5000ms));
            expect(fixture.Press(Event::Character('-')));
            expect(eq(fixture.clock.Interval(), 2000ms));
        };
        "letters choose the sort key"_test = [] {
            Fixture fixture;
            for (auto const& [hotkey, key] : SORT_HOTKEYS) {
                expect(fixture.Press(Event::Character(hotkey))) << hotkey;
                expect(eq(fixture.state.sort_key, key)) << hotkey;
            }
        };
        "q and escape quit"_test = [] {
            Fixture fixture;
            expect(fixture.Press(Event::Character('q')));
            expect(eq(fixture.quit_count, 1));
            expect(fixture.Press(Event::Escape));
            expect(eq(fixture.quit_count, 2));
        };
        "other keys are not handled"_test = [] {
            Fixture fixture;
            expect(!fixture.Press(Event::Character('x')));
            expect(eq(fixture.quit_count, 0));
        };
    } };

    UTest navigation{ "navigation", [] {
        "arrows and j/k move one row"_test = [] {
            Fixture fixture;
            auto const& state{ fixture.state };
            expect(fixture.Press(Event::ArrowDown));
            expect(eq(state.selected, 1));
            expect(fixture.Press(Event::Character('j')));
            expect(eq(state.selected, 2));
            expect(fixture.Press(Event::ArrowUp));
            expect(eq(state.selected, 1));
            expect(fixture.Press(Event::Character('k')));
            expect(eq(state.selected, 0));
        };
        "moving the selection follows its process"_test = [] {
            Fixture fixture;
            expect(fixture.Press(Event::ArrowDown));
            expect(eq(fixture.state.followed_pid.value_or(0), fixture.state.latest.processes[1].pid));
        };
        "home and end jump to the ends"_test = [] {
            Fixture fixture;
            expect(fixture.Press(Event::End));
            expect(eq(fixture.state.selected, LAST_ROW));
            expect(fixture.Press(Event::Home));
            expect(eq(fixture.state.selected, 0));
        };
        "page keys move a visible page"_test = [] {
            Fixture fixture;
            fixture.Render();
            expect(fixture.Press(Event::PageDown));
            expect(eq(fixture.state.selected, LAST_ROW));
            expect(fixture.Press(Event::PageUp));
            expect(eq(fixture.state.selected, 0));
        };
        "the wheel scrolls three rows"_test = [] {
            Fixture fixture;
            expect(fixture.Press(wheel(ftxui::Mouse::WheelDown)));
            expect(eq(fixture.state.selected, WHEEL_STEP));
            expect(fixture.Press(wheel(ftxui::Mouse::WheelUp)));
            expect(eq(fixture.state.selected, 0));
        };
    } };

} // namespace
