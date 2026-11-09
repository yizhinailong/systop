#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace monitor {

    struct StaticInfo {
        std::string host_name;
        std::string os_name;
        std::string cpu_name;
        int physical_cores{ 0 };
        int logical_cores{ 0 };
    };

    struct DriveUsage {
        std::string root;
        std::uint64_t total{ 0 };
        std::uint64_t free{ 0 };
    };

    struct ProcessUsage {
        std::uint32_t pid{ 0 };
        std::string name;
        double cpu{ 0.0 };
        std::uint64_t memory{ 0 };
        std::uint32_t threads{ 0 };
        std::uint32_t handles{ 0 };
    };

    struct Snapshot {
        std::chrono::milliseconds uptime{};
        double cpu_total{ 0.0 };
        std::vector<double> cpu_cores;
        double cpu_mhz{ 0.0 };
        std::uint64_t memory_total{ 0 };
        std::uint64_t memory_used{ 0 };
        std::uint64_t commit_limit{ 0 };
        std::uint64_t commit_used{ 0 };
        std::uint64_t system_cache{ 0 };
        std::uint32_t process_count{ 0 };
        std::uint32_t thread_count{ 0 };
        std::uint32_t handle_count{ 0 };
        double net_receive{ 0.0 };
        double net_send{ 0.0 };
        double disk_read{ 0.0 };
        double disk_write{ 0.0 };
        double disk_active{ 0.0 };
        std::vector<DriveUsage> drives;
        std::vector<ProcessUsage> processes;
    };

    [[nodiscard]] auto query_static_info() -> StaticInfo;

    class Sampler {
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;

    public:
        Sampler();
        ~Sampler();
        Sampler(Sampler const&) = delete;
        auto operator=(Sampler const&) -> Sampler& = delete;

        [[nodiscard]] auto Sample() -> Snapshot;
    };

} // namespace monitor
