import std;
import mcpplibs.mtest;
import systop;

namespace {

    using mcpplibs::mtest::UTest;
    using mcpplibs::mtest::eq;
    using mcpplibs::mtest::expect;
    using mcpplibs::mtest::ge;
    using mcpplibs::mtest::gt;
    using mcpplibs::mtest::le;
    using mcpplibs::mtest::operator""_test;

    constexpr std::string_view TEST_IMAGE{ "test_sampler.exe" };
    constexpr std::chrono::milliseconds SAMPLE_GAP{ 200 };

    auto steady_sample() -> monitor::Snapshot {
        monitor::Sampler sampler;
        std::this_thread::sleep_for(SAMPLE_GAP);
        static_cast<void>(sampler.Sample());
        std::this_thread::sleep_for(SAMPLE_GAP);
        return sampler.Sample();
    }

    UTest static_info{ "query_static_info", [] {
        auto const info{ monitor::query_static_info() };
        "names the host, OS and CPU"_test = [&] {
            expect(!info.host_name.empty());
            expect(info.os_name.starts_with("Windows")) << info.os_name;
            expect(!info.cpu_name.empty());
        };
        "counts physical and logical cores"_test = [&] {
            expect(gt(info.physical_cores, 0));
            expect(ge(info.logical_cores, info.physical_cores));
        };
    } };

    UTest sampling{ "Sample", [] {
        auto const logical_cores{ monitor::query_static_info().logical_cores };
        auto const snapshot{ steady_sample() };
        "reports uptime and memory"_test = [&] {
            expect(gt(snapshot.uptime, std::chrono::milliseconds::zero()));
            expect(gt(snapshot.memory_total, 0u));
            expect(le(snapshot.memory_used, snapshot.memory_total));
            expect(le(snapshot.commit_used, snapshot.commit_limit));
            expect(gt(snapshot.process_count, 0u));
        };
        "reports one CPU load per logical core"_test = [&] {
            expect(eq(std::ssize(snapshot.cpu_cores), logical_cores));
            expect(ge(snapshot.cpu_total, 0.0));
            for (auto const load : snapshot.cpu_cores) {
                expect(ge(load, 0.0));
            }
        };
        "reports rates that are not negative"_test = [&] {
            expect(ge(snapshot.net_receive, 0.0));
            expect(ge(snapshot.net_send, 0.0));
            expect(ge(snapshot.disk_read, 0.0));
            expect(ge(snapshot.disk_write, 0.0));
            expect(ge(snapshot.disk_active, 0.0));
            expect(le(snapshot.disk_active, 100.0));
        };
        "reports fixed drives by their letters"_test = [&] {
            expect(!snapshot.drives.empty());
            for (auto const& drive : snapshot.drives) {
                expect(drive.root.ends_with(':')) << drive.root;
                expect(le(drive.free, drive.total)) << drive.root;
            }
        };
        "lists running processes"_test = [&] {
            expect(!snapshot.processes.empty());
            expect(std::ranges::contains(snapshot.processes, TEST_IMAGE, &monitor::ProcessUsage::name)) << TEST_IMAGE;
            for (auto const& process : snapshot.processes) {
                expect(gt(process.pid, 0u)) << process.name;
                expect(ge(process.cpu, 0.0)) << process.name;
                expect(le(process.cpu, 100.0)) << process.name;
            }
        };
    } };

} // namespace
