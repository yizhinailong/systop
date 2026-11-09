#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/task.hpp>

namespace monitor {

    struct MonitorState;
    class RefreshClock;

    [[nodiscard]] auto make_monitor(MonitorState& state, RefreshClock& clock, ftxui::Closure quit) -> ftxui::Component;

} // namespace monitor
