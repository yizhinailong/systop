#pragma once

#include <cstddef>
#include <deque>

namespace monitor {

    inline constexpr std::size_t HISTORY_CAPACITY{ 1024 };

    class History {
    private:
        std::deque<double> m_values;

    public:
        auto Push(double value) -> void {
            m_values.push_back(value);
            if (m_values.size() > HISTORY_CAPACITY) {
                m_values.pop_front();
            }
        }

        [[nodiscard]] auto Values() const noexcept -> std::deque<double> const& {
            return m_values;
        }
    };

} // namespace monitor
