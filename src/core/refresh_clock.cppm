export module systop:refresh_clock;

import std;

export namespace monitor {

    class RefreshClock {
    private:
        mutable std::mutex m_mutex;
        std::condition_variable_any m_changed;
        std::size_t m_index;
        std::uint64_t m_generation{ 0 };

    public:
        RefreshClock();

        [[nodiscard]] auto Interval() const -> std::chrono::milliseconds;
        auto Step(int delta) -> void;
        [[nodiscard]] auto WaitForTick(std::stop_token const& stop) -> bool;
    };

} // namespace monitor

namespace monitor {

    namespace {

        constexpr std::array<std::chrono::milliseconds, 4> INTERVALS{
            std::chrono::milliseconds{ 500 },
            std::chrono::milliseconds{ 1000 },
            std::chrono::milliseconds{ 2000 },
            std::chrono::milliseconds{ 5000 },
        };

        constexpr std::size_t DEFAULT_INTERVAL_INDEX{ 1 };

    } // namespace

    RefreshClock::RefreshClock() : m_index{ DEFAULT_INTERVAL_INDEX } {}

    auto RefreshClock::Interval() const -> std::chrono::milliseconds {
        std::scoped_lock const lock{ m_mutex };
        return INTERVALS[m_index];
    }

    auto RefreshClock::Step(int delta) -> void {
        {
            std::scoped_lock const lock{ m_mutex };
            auto const last{ static_cast<int>(INTERVALS.size()) - 1 };
            auto const next{ static_cast<std::size_t>(std::clamp(static_cast<int>(m_index) + delta, 0, last)) };
            if (next == m_index) {
                return;
            }
            m_index = next;
            ++m_generation;
        }
        m_changed.notify_all();
    }

    auto RefreshClock::WaitForTick(std::stop_token const& stop) -> bool {
        std::unique_lock lock{ m_mutex };
        auto const generation{ m_generation };
        m_changed.wait_for(lock, stop, INTERVALS[m_index], [this, generation] { return m_generation != generation; });
        return !stop.stop_requested();
    }

} // namespace monitor
