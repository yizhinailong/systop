#include "sampler.hpp"

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <pdh.h>
#include <psapi.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace monitor {

    namespace {

        constexpr double TICKS_PER_SECOND{ 10'000'000.0 };
        constexpr unsigned long FIRST_WINDOWS_11_BUILD{ 22000 };
        constexpr NTSTATUS INFO_LENGTH_MISMATCH{ static_cast<NTSTATUS>(0xC0000004UL) };
        constexpr std::size_t INITIAL_PROCESS_BUFFER{ 512 * 1024 };
        constexpr int PROCESS_QUERY_ATTEMPTS{ 4 };

        struct ProcessEntry {
            ULONG next_entry_offset;
            ULONG thread_count;
            LARGE_INTEGER private_working_set;
            ULONG hard_fault_count;
            ULONG thread_count_high_watermark;
            ULONGLONG cycle_time;
            LARGE_INTEGER create_time;
            LARGE_INTEGER user_time;
            LARGE_INTEGER kernel_time;
            UNICODE_STRING image_name;
            LONG base_priority;
            HANDLE process_id;
            HANDLE parent_process_id;
            ULONG handle_count;
        };

        static_assert(offsetof(ProcessEntry, image_name) == offsetof(SYSTEM_PROCESS_INFORMATION, ImageName));
        static_assert(offsetof(ProcessEntry, process_id) == offsetof(SYSTEM_PROCESS_INFORMATION, UniqueProcessId));
        static_assert(offsetof(ProcessEntry, handle_count) == offsetof(SYSTEM_PROCESS_INFORMATION, HandleCount));

        struct CpuTimes {
            std::int64_t created{ 0 };
            std::int64_t used{ 0 };
        };

        struct NetworkTotals {
            std::uint64_t received{ 0 };
            std::uint64_t sent{ 0 };
        };

        auto to_utf8(std::wstring_view text) -> std::string {
            if (text.empty()) {
                return {};
            }
            auto const source_size{ static_cast<int>(text.size()) };
            auto const size{ WideCharToMultiByte(CP_UTF8, 0, text.data(), source_size, nullptr, 0, nullptr, nullptr) };
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), source_size, result.data(), size, nullptr, nullptr);
            return result;
        }

        auto trim(std::wstring_view text) -> std::wstring_view {
            auto const first{ text.find_first_not_of(L' ') };
            if (first == std::wstring_view::npos) {
                return {};
            }
            return text.substr(first, text.find_last_not_of(L' ') - first + 1);
        }

        auto read_registry_string(wchar_t const* key, wchar_t const* value) -> std::wstring {
            DWORD size{ 0 };
            if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) {
                return {};
            }
            std::wstring text(size / sizeof(wchar_t), L'\0');
            if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, text.data(), &size) != ERROR_SUCCESS) {
                return {};
            }
            text.resize(std::wcslen(text.c_str()));
            return text;
        }

        auto read_host_name() -> std::string {
            DWORD size{ 0 };
            GetComputerNameExW(ComputerNameDnsHostname, nullptr, &size);
            std::wstring name(size, L'\0');
            if (!GetComputerNameExW(ComputerNameDnsHostname, name.data(), &size)) {
                return {};
            }
            name.resize(size);
            return to_utf8(name);
        }

        auto read_os_name() -> std::string {
            constexpr wchar_t const* KEY{ L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion" };
            constexpr std::wstring_view LEGACY_PRODUCT{ L"Windows 10" };

            auto product{ read_registry_string(KEY, L"ProductName") };
            auto const build{ read_registry_string(KEY, L"CurrentBuildNumber") };
            auto const release{ read_registry_string(KEY, L"DisplayVersion") };
            if (std::wcstoul(build.c_str(), nullptr, 10) >= FIRST_WINDOWS_11_BUILD && product.starts_with(LEGACY_PRODUCT)) {
                product.replace(0, LEGACY_PRODUCT.size(), L"Windows 11");
            }
            if (!release.empty()) {
                product += std::format(L" {}", release);
            }
            if (!build.empty()) {
                product += std::format(L" ({})", build);
            }
            return to_utf8(product);
        }

        auto read_cpu_name() -> std::string {
            auto const name{ read_registry_string(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString") };
            return to_utf8(trim(name));
        }

        auto count_physical_cores() -> int {
            DWORD size{ 0 };
            GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
            std::vector<std::byte> buffer(size);
            auto* const first{ reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()) };
            if (!GetLogicalProcessorInformationEx(RelationProcessorCore, first, &size)) {
                return 0;
            }
            int cores{ 0 };
            for (DWORD offset{ 0 }; offset < size; ++cores) {
                offset += reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX const*>(buffer.data() + offset)->Size;
            }
            return cores;
        }

        auto read_counter(PDH_HCOUNTER counter, DWORD format = PDH_FMT_DOUBLE) -> std::optional<double> {
            if (counter == nullptr) {
                return std::nullopt;
            }
            PDH_FMT_COUNTERVALUE value{};
            if (PdhGetFormattedCounterValue(counter, format, nullptr, &value) != ERROR_SUCCESS) {
                return std::nullopt;
            }
            return value.doubleValue;
        }

        auto read_memory(Snapshot& snapshot) -> void {
            PERFORMANCE_INFORMATION info{ .cb = sizeof(PERFORMANCE_INFORMATION) };
            if (!GetPerformanceInfo(&info, sizeof(info))) {
                return;
            }
            auto const page{ static_cast<std::uint64_t>(info.PageSize) };
            snapshot.memory_total = info.PhysicalTotal * page;
            snapshot.memory_used = (info.PhysicalTotal - info.PhysicalAvailable) * page;
            snapshot.commit_limit = info.CommitLimit * page;
            snapshot.commit_used = info.CommitTotal * page;
            snapshot.system_cache = info.SystemCache * page;
            snapshot.process_count = info.ProcessCount;
            snapshot.thread_count = info.ThreadCount;
            snapshot.handle_count = info.HandleCount;
        }

        auto read_network_totals() -> NetworkTotals {
            PMIB_IF_TABLE2 table{ nullptr };
            if (GetIfTable2(&table) != NO_ERROR) {
                return {};
            }
            NetworkTotals totals{};
            for (auto const& row : std::span{ table->Table, table->NumEntries }) {
                auto const& flags{ row.InterfaceAndOperStatusFlags };
                if (!flags.HardwareInterface || flags.FilterInterface || row.OperStatus != IfOperStatusUp) {
                    continue;
                }
                totals.received += row.InOctets;
                totals.sent += row.OutOctets;
            }
            FreeMibTable(table);
            return totals;
        }

        auto rate(std::uint64_t previous, std::uint64_t current, double seconds) -> double {
            if (current < previous) {
                return 0.0;
            }
            return static_cast<double>(current - previous) / seconds;
        }

        auto read_drives() -> std::vector<DriveUsage> {
            std::array<wchar_t, 512> roots{};
            auto const length{ GetLogicalDriveStringsW(static_cast<DWORD>(roots.size()), roots.data()) };
            if (length == 0 || length >= roots.size()) {
                return {};
            }
            std::vector<DriveUsage> drives;
            for (wchar_t const* root{ roots.data() }; *root != L'\0'; root += std::wcslen(root) + 1) {
                if (GetDriveTypeW(root) != DRIVE_FIXED) {
                    continue;
                }
                ULARGE_INTEGER total{};
                ULARGE_INTEGER free_bytes{};
                if (!GetDiskFreeSpaceExW(root, nullptr, &total, &free_bytes)) {
                    continue;
                }
                std::wstring_view name{ root };
                name.remove_suffix(1);
                drives.push_back({
                    .root = to_utf8(name),
                    .total = total.QuadPart,
                    .free = free_bytes.QuadPart,
                });
            }
            return drives;
        }

        class PerformanceCounters {
        private:
            PDH_HQUERY m_query{ nullptr };
            PDH_HCOUNTER m_cpu_total{ nullptr };
            std::vector<PDH_HCOUNTER> m_cpu_cores;
            PDH_HCOUNTER m_cpu_frequency{ nullptr };
            PDH_HCOUNTER m_cpu_performance{ nullptr };
            PDH_HCOUNTER m_disk_read{ nullptr };
            PDH_HCOUNTER m_disk_write{ nullptr };
            PDH_HCOUNTER m_disk_idle{ nullptr };

        public:
            PerformanceCounters() {
                if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) {
                    m_query = nullptr;
                    return;
                }
                m_cpu_total = addCpuCounter(L"_Total");
                auto const groups{ GetActiveProcessorGroupCount() };
                for (WORD group{ 0 }; group < groups; ++group) {
                    auto const count{ GetActiveProcessorCount(group) };
                    for (DWORD index{ 0 }; index < count; ++index) {
                        m_cpu_cores.push_back(addCpuCounter(std::format(L"{},{}", group, index)));
                    }
                }
                m_cpu_frequency = addCounter(L"\\Processor Information(_Total)\\Processor Frequency");
                m_cpu_performance = addCounter(L"\\Processor Information(_Total)\\% Processor Performance");
                m_disk_read = addCounter(L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec");
                m_disk_write = addCounter(L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec");
                m_disk_idle = addCounter(L"\\PhysicalDisk(_Total)\\% Idle Time");
                PdhCollectQueryData(m_query);
            }

            ~PerformanceCounters() {
                if (m_query != nullptr) {
                    PdhCloseQuery(m_query);
                }
            }

            PerformanceCounters(PerformanceCounters const&) = delete;
            auto operator=(PerformanceCounters const&) -> PerformanceCounters& = delete;

            auto Collect(Snapshot& snapshot) const -> void {
                if (m_query == nullptr || PdhCollectQueryData(m_query) != ERROR_SUCCESS) {
                    return;
                }
                snapshot.cpu_total = read_counter(m_cpu_total).value_or(0.0);
                snapshot.cpu_cores.reserve(m_cpu_cores.size());
                for (auto const counter : m_cpu_cores) {
                    snapshot.cpu_cores.push_back(read_counter(counter).value_or(0.0));
                }
                auto const frequency{ read_counter(m_cpu_frequency).value_or(0.0) };
                auto const performance{ read_counter(m_cpu_performance, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100).value_or(0.0) };
                snapshot.cpu_mhz = frequency * performance / 100.0;
                snapshot.disk_read = read_counter(m_disk_read).value_or(0.0);
                snapshot.disk_write = read_counter(m_disk_write).value_or(0.0);
                snapshot.disk_active = read_counter(m_disk_idle)
                                           .transform([](double idle) { return std::clamp(100.0 - idle, 0.0, 100.0); })
                                           .value_or(0.0);
            }

        private:
            auto addCounter(std::wstring const& path) -> PDH_HCOUNTER {
                PDH_HCOUNTER counter{ nullptr };
                if (PdhAddEnglishCounterW(m_query, path.c_str(), 0, &counter) != ERROR_SUCCESS) {
                    return nullptr;
                }
                return counter;
            }

            auto addCpuCounter(std::wstring_view instance) -> PDH_HCOUNTER {
                if (auto const counter{ addCounter(std::format(L"\\Processor Information({})\\% Processor Utility", instance)) }) {
                    return counter;
                }
                return addCounter(std::format(L"\\Processor Information({})\\% Processor Time", instance));
            }
        };

        class ProcessTracker {
        private:
            std::vector<std::byte> m_buffer;
            std::unordered_map<std::uint32_t, CpuTimes> m_previous;

        public:
            auto Sample(double elapsed_seconds, int logical_cores) -> std::vector<ProcessUsage> {
                if (!refresh()) {
                    return {};
                }
                auto const budget{ elapsed_seconds * TICKS_PER_SECOND * logical_cores };
                std::vector<ProcessUsage> processes;
                std::unordered_map<std::uint32_t, CpuTimes> current;
                std::size_t offset{ 0 };
                while (true) {
                    auto const& entry{ *reinterpret_cast<ProcessEntry const*>(m_buffer.data() + offset) };
                    auto const pid{ static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(entry.process_id)) };
                    if (pid != 0) {
                        CpuTimes const times{
                            .created = entry.create_time.QuadPart,
                            .used = entry.kernel_time.QuadPart + entry.user_time.QuadPart,
                        };
                        processes.push_back({
                            .pid = pid,
                            .name = to_utf8({ entry.image_name.Buffer, entry.image_name.Length / sizeof(wchar_t) }),
                            .cpu = cpuPercent(pid, times, budget),
                            .memory = static_cast<std::uint64_t>(entry.private_working_set.QuadPart),
                            .threads = entry.thread_count,
                            .handles = entry.handle_count,
                        });
                        current.emplace(pid, times);
                    }
                    if (entry.next_entry_offset == 0) {
                        break;
                    }
                    offset += entry.next_entry_offset;
                }
                m_previous = std::move(current);
                return processes;
            }

        private:
            auto refresh() -> bool {
                if (m_buffer.empty()) {
                    m_buffer.resize(INITIAL_PROCESS_BUFFER);
                }
                for (int attempt{ 0 }; attempt < PROCESS_QUERY_ATTEMPTS; ++attempt) {
                    ULONG needed{ 0 };
                    auto const status{ NtQuerySystemInformation(SystemProcessInformation, m_buffer.data(), static_cast<ULONG>(m_buffer.size()), &needed) };
                    if (status >= 0) {
                        return true;
                    }
                    if (status != INFO_LENGTH_MISMATCH) {
                        return false;
                    }
                    m_buffer.resize(std::max<std::size_t>(needed + needed / 4, m_buffer.size() * 2));
                }
                return false;
            }

            auto cpuPercent(std::uint32_t pid, CpuTimes const& times, double budget) const -> double {
                auto const previous{ m_previous.find(pid) };
                if (previous == m_previous.end() || previous->second.created != times.created) {
                    return 0.0;
                }
                return std::clamp(static_cast<double>(times.used - previous->second.used) / budget * 100.0, 0.0, 100.0);
            }
        };

    } // namespace

    struct Sampler::Impl {
        PerformanceCounters counters;
        ProcessTracker processes;
        NetworkTotals network{ read_network_totals() };
        std::chrono::steady_clock::time_point last_sample{ std::chrono::steady_clock::now() };
        int logical_cores{ static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)) };
    };

    auto query_static_info() -> StaticInfo {
        return {
            .host_name = read_host_name(),
            .os_name = read_os_name(),
            .cpu_name = read_cpu_name(),
            .physical_cores = count_physical_cores(),
            .logical_cores = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)),
        };
    }

    Sampler::Sampler() : m_impl{ std::make_unique<Impl>() } {
        m_impl->processes.Sample(1.0, m_impl->logical_cores);
    }

    Sampler::~Sampler() = default;

    auto Sampler::Sample() -> Snapshot {
        auto const now{ std::chrono::steady_clock::now() };
        auto const elapsed{ std::chrono::duration<double>{ now - m_impl->last_sample }.count() };
        m_impl->last_sample = now;

        Snapshot snapshot{};
        snapshot.uptime = std::chrono::milliseconds{ static_cast<std::int64_t>(GetTickCount64()) };
        m_impl->counters.Collect(snapshot);
        read_memory(snapshot);

        auto const network{ read_network_totals() };
        snapshot.net_receive = rate(m_impl->network.received, network.received, elapsed);
        snapshot.net_send = rate(m_impl->network.sent, network.sent, elapsed);
        m_impl->network = network;

        snapshot.drives = read_drives();
        snapshot.processes = m_impl->processes.Sample(elapsed, m_impl->logical_cores);
        return snapshot;
    }

} // namespace monitor
