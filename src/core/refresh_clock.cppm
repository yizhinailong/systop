export module systop:refresh_clock;

import std;

export namespace monitor {

    inline constexpr std::array<std::chrono::milliseconds, 4> REFRESH_INTERVALS{
        std::chrono::milliseconds{ 500 },
        std::chrono::milliseconds{ 1000 },
        std::chrono::milliseconds{ 2000 },
        std::chrono::milliseconds{ 5000 },
    };

    inline constexpr std::chrono::milliseconds DEFAULT_REFRESH_INTERVAL{ REFRESH_INTERVALS[1] };

    class RefreshClock {
    private:
        mutable std::mutex m_mutex;
        std::condition_variable_any m_changed;
        std::size_t m_index;
        std::uint64_t m_generation{ 0 };

    public:
        explicit RefreshClock(std::chrono::milliseconds interval = DEFAULT_REFRESH_INTERVAL);

        [[nodiscard]] auto Interval() const -> std::chrono::milliseconds;
        auto Step(int delta) -> void;
        [[nodiscard]] auto WaitForTick(std::stop_token const& stop) -> bool;
    };

} // namespace monitor

namespace monitor {

    namespace {

        auto nearest_interval_index(std::chrono::milliseconds interval) -> std::size_t {
            auto const distance{ [interval](std::chrono::milliseconds preset) { return std::chrono::abs(preset - interval); } };
            auto const nearest{ std::ranges::min_element(REFRESH_INTERVALS, {}, distance) };
            return static_cast<std::size_t>(nearest - REFRESH_INTERVALS.begin());
        }

    } // namespace

    RefreshClock::RefreshClock(std::chrono::milliseconds interval) : m_index{ nearest_interval_index(interval) } {}

    auto RefreshClock::Interval() const -> std::chrono::milliseconds {
        std::scoped_lock const lock{ m_mutex };
        return REFRESH_INTERVALS[m_index];
    }

    auto RefreshClock::Step(int delta) -> void {
        {
            std::scoped_lock const lock{ m_mutex };
            auto const last{ static_cast<int>(REFRESH_INTERVALS.size()) - 1 };
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
        m_changed.wait_for(lock, stop, REFRESH_INTERVALS[m_index], [this, generation] { return m_generation != generation; });
        return !stop.stop_requested();
    }

} // namespace monitor
