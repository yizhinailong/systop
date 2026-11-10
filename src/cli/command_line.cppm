export module systop:command_line;

import :monitor_state;
import :refresh_clock;
import :version;
import mcpplibs.cmdline;
import std;

export namespace monitor {

    struct LaunchOptions {
        std::chrono::milliseconds interval{ DEFAULT_REFRESH_INTERVAL };
        SortKey sort_key{ SortKey::Cpu };
    };

    struct CommandLineExit {
        int code{ 0 };
        std::string message;
    };

    [[nodiscard]] auto parse_command_line(std::span<std::string const> args) -> std::expected<LaunchOptions, CommandLineExit>;

} // namespace monitor

namespace monitor {

    namespace {

        constexpr int USAGE_ERROR{ 2 };

        constexpr std::array<std::pair<std::string_view, SortKey>, 4> SORT_NAMES{
            {
             { "cpu", SortKey::Cpu },
             { "memory", SortKey::Memory },
             { "name", SortKey::Name },
             { "pid", SortKey::Pid },
             }
        };

        auto to_seconds(std::chrono::milliseconds interval) -> double {
            return std::chrono::duration<double>{ interval }.count();
        }

        auto interval_choices() -> std::string {
            return std::format("{:n}", REFRESH_INTERVALS | std::views::transform(to_seconds));
        }

        auto sort_choices() -> std::string {
            return std::format("{:n:}", SORT_NAMES | std::views::keys);
        }

        auto make_app() -> mcpplibs::cmdline::App {
            return mcpplibs::cmdline::App{ "systop" }
                .version(VERSION)
                .description("Terminal resource monitor for Windows")
                .option("interval")
                .short_name('i')
                .takes_value()
                .value_name("SEC")
                .help(std::format("Refresh interval in seconds: {} (default {})", interval_choices(), to_seconds(DEFAULT_REFRESH_INTERVAL)))
                .option("sort")
                .short_name('s')
                .takes_value()
                .value_name("KEY")
                .help(std::format("Process sort order: {} (default cpu)", sort_choices()))
                .end();
        }

        auto usage_error(std::string message) -> std::unexpected<CommandLineExit> {
            return std::unexpected{ CommandLineExit{ .code = USAGE_ERROR, .message = std::move(message) } };
        }

        auto parse_interval(std::string_view text) -> std::optional<std::chrono::milliseconds> {
            double seconds{ 0.0 };
            auto const* const last{ text.data() + text.size() };
            auto const [end, error]{ std::from_chars(text.data(), last, seconds) };
            if (error != std::errc{} || end != last) {
                return std::nullopt;
            }
            std::chrono::duration<double> const requested{ seconds };
            auto const found{ std::ranges::find_if(REFRESH_INTERVALS, [requested](std::chrono::milliseconds preset) { return preset == requested; }) };
            if (found == REFRESH_INTERVALS.end()) {
                return std::nullopt;
            }
            return *found;
        }

        auto parse_sort_key(std::string_view text) -> std::optional<SortKey> {
            auto const found{ std::ranges::find(SORT_NAMES, text, &std::pair<std::string_view, SortKey>::first) };
            if (found == SORT_NAMES.end()) {
                return std::nullopt;
            }
            return found->second;
        }

    } // namespace

    auto parse_command_line(std::span<std::string const> args) -> std::expected<LaunchOptions, CommandLineExit> {
        auto const parsed{ make_app().parse_from(args) };
        if (!parsed) {
            if (!parsed.error().is_error()) {
                return std::unexpected{ CommandLineExit{} };
            }
            return usage_error(parsed.error().message);
        }
        if (parsed->positional_count() > 0) {
            return usage_error(std::format("unexpected argument '{}'", parsed->positional(0)));
        }

        LaunchOptions options{};
        if (auto const text{ parsed->value("interval") }) {
            auto const interval{ parse_interval(*text) };
            if (!interval) {
                return usage_error(std::format("invalid interval '{}': expected one of {}", *text, interval_choices()));
            }
            options.interval = *interval;
        }
        if (auto const text{ parsed->value("sort") }) {
            auto const sort_key{ parse_sort_key(*text) };
            if (!sort_key) {
                return usage_error(std::format("invalid sort key '{}': expected one of {}", *text, sort_choices()));
            }
            options.sort_key = *sort_key;
        }
        return options;
    }

} // namespace monitor
