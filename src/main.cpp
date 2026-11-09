#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>

#include "monitor_state.hpp"
#include "monitor_view.hpp"
#include "sampler.hpp"

namespace {

    constexpr std::chrono::milliseconds FIRST_SAMPLE_DELAY{ 250 };

    auto wait_or_stop(std::stop_token const& stop, std::chrono::milliseconds delay) -> bool {
        std::mutex mutex{};
        std::condition_variable_any wake{};
        std::unique_lock lock{ mutex };
        return wake.wait_for(lock, stop, delay, [&stop] { return stop.stop_requested(); });
    }

} // namespace

auto main() -> int {
    monitor::MonitorState state{ monitor::query_static_info() };
    auto app{ ftxui::App::Fullscreen() };
    auto root{ monitor::make_monitor(state, app.ExitLoopClosure()) };

    // Declared after `app` so it is stopped and joined before `app` is destroyed.
    std::jthread sampling{ [&app, &state](std::stop_token stop) {
        monitor::Sampler sampler{};
        for (auto delay{ FIRST_SAMPLE_DELAY }; !wait_or_stop(stop, delay); delay = state.Interval()) {
            app.Post([&state, snapshot = sampler.Sample()]() mutable { state.Apply(std::move(snapshot)); });
            app.PostEvent(ftxui::Event::Custom);
        }
    } };

    app.Loop(root);
    return 0;
}
