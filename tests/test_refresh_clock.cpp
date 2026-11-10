import std;
import mcpplibs.mtest;
import systop;

namespace {

    using namespace std::chrono_literals;

    using mcpplibs::mtest::UTest;
    using mcpplibs::mtest::eq;
    using mcpplibs::mtest::expect;
    using mcpplibs::mtest::ge;
    using mcpplibs::mtest::lt;
    using mcpplibs::mtest::timeout;
    using mcpplibs::mtest::operator""_test;

    using Clock = std::chrono::steady_clock;

    constexpr int INTERVAL_COUNT{ 4 };
    constexpr std::chrono::milliseconds SETTLE_DELAY{ 100 };
    constexpr std::chrono::milliseconds WAKE_LIMIT{ 2500 };
    constexpr std::chrono::seconds WAIT_TIMEOUT{ 10 };

    struct WaitResult {
        bool ticked{ false };
        Clock::duration waited{};
    };

    auto start_waiter(monitor::RefreshClock& clock, WaitResult& result) -> std::jthread {
        return std::jthread{ [&clock, &result](std::stop_token stop) {
            auto const start{ Clock::now() };
            result.ticked = clock.WaitForTick(stop);
            result.waited = Clock::now() - start;
        } };
    }

    UTest stepping{ "Step", [] {
        "starts at one second"_test = [] {
            expect(eq(monitor::RefreshClock{}.Interval(), 1000ms));
        };
        "moves one interval per step"_test = [] {
            monitor::RefreshClock clock;
            clock.Step(1);
            expect(eq(clock.Interval(), 2000ms));
            clock.Step(-2);
            expect(eq(clock.Interval(), 500ms));
        };
        "clamps at both ends"_test = [] {
            monitor::RefreshClock clock;
            clock.Step(INTERVAL_COUNT);
            expect(eq(clock.Interval(), 5000ms));
            clock.Step(-INTERVAL_COUNT);
            expect(eq(clock.Interval(), 500ms));
        };
    } };

    UTest waiting{ "WaitForTick", timeout{ WAIT_TIMEOUT }, [] {
        "ticks after a whole interval"_test = [] {
            monitor::RefreshClock clock;
            clock.Step(-INTERVAL_COUNT);
            std::stop_source source;
            auto const start{ Clock::now() };
            expect(clock.WaitForTick(source.get_token()));
            expect(ge(Clock::now() - start, clock.Interval()));
        };
        "wakes when the interval changes"_test = [] {
            monitor::RefreshClock clock;
            clock.Step(INTERVAL_COUNT);
            WaitResult result;
            auto waiter{ start_waiter(clock, result) };
            std::this_thread::sleep_for(SETTLE_DELAY);
            clock.Step(-INTERVAL_COUNT);
            waiter.join();
            expect(result.ticked);
            expect(lt(result.waited, WAKE_LIMIT));
        };
        "wakes when stopped"_test = [] {
            monitor::RefreshClock clock;
            clock.Step(INTERVAL_COUNT);
            WaitResult result;
            auto waiter{ start_waiter(clock, result) };
            std::this_thread::sleep_for(SETTLE_DELAY);
            waiter.request_stop();
            waiter.join();
            expect(!result.ticked);
            expect(lt(result.waited, WAKE_LIMIT));
        };
        "returns at once when already stopped"_test = [] {
            monitor::RefreshClock clock;
            std::stop_source source;
            source.request_stop();
            expect(!clock.WaitForTick(source.get_token()));
        };
    } };

} // namespace
