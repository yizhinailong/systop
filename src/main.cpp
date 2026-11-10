#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>

import std;
import systop;

namespace {

    constexpr std::chrono::milliseconds FIRST_SAMPLE_DELAY{ 250 };

} // namespace

auto main() -> int {
    monitor::MonitorState state{ .info = monitor::query_static_info() };
    monitor::RefreshClock clock{};
    auto app{ ftxui::App::Fullscreen() };
    auto root{ monitor::make_monitor(state, clock, app.ExitLoopClosure()) };

    // Declared after `app` so it is stopped and joined before `app` is destroyed.
    std::jthread sampling{ [&app, &state, &clock](std::stop_token stop) {
        monitor::Sampler sampler{};
        std::this_thread::sleep_for(FIRST_SAMPLE_DELAY);
        do {
            auto snapshot{ std::make_shared<monitor::Snapshot>(sampler.Sample()) };
            app.Post([&state, snapshot] { state.Apply(std::move(*snapshot)); });
            app.PostEvent(ftxui::Event::Custom);
        } while (clock.WaitForTick(stop));
    } };

    app.Loop(root);
    return 0;
}
