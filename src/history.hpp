#pragma once

#include <cstddef>
#include <deque>

namespace monitor {

    class History {
    private:
        std::deque<double> m_values;
        std::size_t m_capacity;

    public:
        explicit History(std::size_t capacity) : m_capacity{ capacity } {}

        auto Push(double value) -> void {
            m_values.push_back(value);
            if (m_values.size() > m_capacity) {
                m_values.pop_front();
            }
        }

        [[nodiscard]] auto Values() const noexcept -> std::deque<double> const& {
            return m_values;
        }
    };

} // namespace monitor
