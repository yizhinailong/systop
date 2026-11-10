import std;
import mcpplibs.mtest;
import systop;

namespace {

    using mcpplibs::mtest::UTest;
    using mcpplibs::mtest::eq;
    using mcpplibs::mtest::expect;
    using mcpplibs::mtest::operator""_test;

    using monitor::MonitorState;
    using monitor::ProcessUsage;
    using monitor::Snapshot;
    using monitor::SortKey;

    using Pids = std::vector<std::uint32_t>;

    static_assert(monitor::is_descending(SortKey::Cpu));
    static_assert(monitor::is_descending(SortKey::Memory));
    static_assert(!monitor::is_descending(SortKey::Name));
    static_assert(!monitor::is_descending(SortKey::Pid));

    auto process(std::uint32_t pid, std::string_view name, double cpu, std::uint64_t memory) -> ProcessUsage {
        return { .pid = pid, .name = std::string{ name }, .cpu = cpu, .memory = memory };
    }

    auto sample_snapshot() -> Snapshot {
        return {
            .cpu_total = 42.0,
            .memory_total = 8000,
            .memory_used = 2000,
            .net_receive = 10.0,
            .net_send = 20.0,
            .disk_read = 30.0,
            .disk_write = 40.0,
            .processes = {
                process(30, "Beta.exe", 5.0, 300),
                process(40, "ALPHA.EXE", 0.0, 100),
                process(20, "gamma.exe", 1.0, 900),
                process(10, "alpha.exe", 5.0, 100),
            },
        };
    }

    auto applied(SortKey key = SortKey::Cpu) -> MonitorState {
        MonitorState state{ .sort_key = key };
        state.Apply(sample_snapshot());
        return state;
    }

    auto pids(MonitorState const& state) -> Pids {
        return state.latest.processes | std::views::transform(&ProcessUsage::pid) | std::ranges::to<std::vector>();
    }

    UTest percent{ "percent_of", [] {
        "scales a ratio to 100"_test = [] {
            expect(eq(monitor::percent_of(1, 4), 25.0));
        };
        "treats an empty whole as zero"_test = [] {
            expect(eq(monitor::percent_of(5, 0), 0.0));
        };
    } };

    UTest applying{ "Apply", [] {
        "records every history"_test = [] {
            auto const state{ applied() };
            expect(eq(state.cpu_history.Values(), std::deque{ 42.0 }));
            expect(eq(state.memory_history.Values(), std::deque{ 25.0 }));
            expect(eq(state.net_receive_history.Values(), std::deque{ 10.0 }));
            expect(eq(state.net_send_history.Values(), std::deque{ 20.0 }));
            expect(eq(state.disk_read_history.Values(), std::deque{ 30.0 }));
            expect(eq(state.disk_write_history.Values(), std::deque{ 40.0 }));
        };
        "keeps the snapshot while paused"_test = [] {
            auto state{ applied() };
            state.paused = true;
            auto next{ sample_snapshot() };
            next.cpu_total = 99.0;
            state.Apply(std::move(next));
            expect(eq(state.latest.cpu_total, 42.0));
            expect(eq(state.cpu_history.Values(), std::deque{ 42.0 }));
        };
        "moves the selection with its process"_test = [] {
            auto state{ applied() };
            state.Select(2);
            auto busier{ sample_snapshot() };
            std::ranges::find(busier.processes, 20u, &ProcessUsage::pid)->cpu = 50.0;
            state.Apply(std::move(busier));
            expect(eq(state.selected, 0));
            expect(eq(state.followed_pid.value_or(0), 20u));
        };
        "stops following a process that exits"_test = [] {
            auto state{ applied() };
            state.Select(3);
            auto shrunk{ sample_snapshot() };
            std::erase_if(shrunk.processes, [](ProcessUsage const& usage) { return usage.pid == 40 || usage.pid == 20; });
            state.Apply(std::move(shrunk));
            expect(!state.followed_pid);
            expect(eq(state.selected, 1));
        };
        "selects the first row of an empty list"_test = [] {
            auto state{ applied() };
            state.Select(3);
            state.Apply(Snapshot{});
            expect(eq(state.selected, 0));
        };
    } };

    UTest ordering{ "sort order", [] {
        "CPU load descends, then memory"_test = [] {
            expect(eq(pids(applied(SortKey::Cpu)), Pids{ 30, 10, 20, 40 }));
        };
        "memory descends, then PID ascends"_test = [] {
            expect(eq(pids(applied(SortKey::Memory)), Pids{ 20, 30, 10, 40 }));
        };
        "names ignore ASCII case, then PID ascends"_test = [] {
            expect(eq(pids(applied(SortKey::Name)), Pids{ 10, 40, 30, 20 }));
        };
        "PIDs ascend"_test = [] {
            expect(eq(pids(applied(SortKey::Pid)), Pids{ 10, 20, 30, 40 }));
        };
    } };

    UTest sorting{ "SortBy", [] {
        "re-sorts the current list"_test = [] {
            auto state{ applied() };
            state.SortBy(SortKey::Pid);
            expect(eq(state.sort_key, SortKey::Pid));
            expect(eq(pids(state), Pids{ 10, 20, 30, 40 }));
        };
        "resets the selection"_test = [] {
            auto state{ applied() };
            state.Select(2);
            state.SortBy(SortKey::Name);
            expect(eq(state.selected, 0));
            expect(!state.followed_pid);
        };
    } };

    UTest selecting{ "Select", [] {
        "does nothing on an empty list"_test = [] {
            MonitorState state;
            state.Select(3);
            expect(eq(state.selected, 0));
            expect(!state.followed_pid);
        };
        "follows the selected process"_test = [] {
            auto state{ applied() };
            state.Select(2);
            expect(eq(state.selected, 2));
            expect(eq(state.followed_pid.value_or(0), 20u));
        };
        "clamps to the list"_test = [] {
            auto state{ applied() };
            state.Select(-5);
            expect(eq(state.selected, 0));
            expect(eq(state.followed_pid.value_or(0), 30u));
            state.Select(100);
            expect(eq(state.selected, 3));
            expect(eq(state.followed_pid.value_or(0), 40u));
        };
    } };

} // namespace
