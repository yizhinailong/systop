#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/task.hpp>

namespace monitor {

    struct MonitorState;

    [[nodiscard]] auto make_monitor(MonitorState& state, ftxui::Closure quit) -> ftxui::Component;

} // namespace monitor
