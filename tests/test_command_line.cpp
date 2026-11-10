import std;
import mcpplibs.mtest;
import systop;

namespace {

    using namespace std::chrono_literals;

    using mcpplibs::mtest::UTest;
    using mcpplibs::mtest::eq;
    using mcpplibs::mtest::expect;
    using mcpplibs::mtest::operator""_test;

    using monitor::CommandLineExit;
    using monitor::LaunchOptions;
    using monitor::SortKey;

    constexpr int USAGE_ERROR{ 2 };

    auto parse(std::vector<std::string> args) -> std::expected<LaunchOptions, CommandLineExit> {
        args.insert(args.begin(), "systop");
        return monitor::parse_command_line(args);
    }

    auto parsed_interval(std::string_view text) -> std::chrono::milliseconds {
        return parse({ "--interval", std::string{ text } }).value().interval;
    }

    auto rejected(std::expected<LaunchOptions, CommandLineExit> const& options) -> bool {
        return !options && options.error().code == USAGE_ERROR && !options.error().message.empty();
    }

    UTest defaults{ "defaults", [] {
        auto const options{ parse({}) };
        expect(options.has_value());
        expect(eq(options.value().interval, 1000ms));
        expect(eq(options.value().sort_key, SortKey::Cpu));
    } };

    UTest interval{ "interval", [] {
        "accepts every preset in seconds"_test = [] {
            expect(eq(parsed_interval("0.5"), 500ms));
            expect(eq(parsed_interval("1"), 1000ms));
            expect(eq(parsed_interval("2.0"), 2000ms));
            expect(eq(parsed_interval("5"), 5000ms));
        };
        "accepts the short and inline forms"_test = [] {
            expect(eq(parse({ "-i", "2" }).value().interval, 2000ms));
            expect(eq(parse({ "--interval=0.5" }).value().interval, 500ms));
        };
        "rejects values that are not presets"_test = [] {
            for (auto const text : { "3", "0", "-1", "1s", "abc", "", "nan", "inf" }) {
                expect(rejected(parse({ "--interval", text })));
            }
        };
        "requires a value"_test = [] {
            expect(rejected(parse({ "--interval" })));
        };
    } };

    UTest sort{ "sort", [] {
        "accepts every key"_test = [] {
            expect(eq(parse({ "--sort", "cpu" }).value().sort_key, SortKey::Cpu));
            expect(eq(parse({ "--sort", "memory" }).value().sort_key, SortKey::Memory));
            expect(eq(parse({ "-s", "name" }).value().sort_key, SortKey::Name));
            expect(eq(parse({ "--sort=pid" }).value().sort_key, SortKey::Pid));
        };
        "rejects unknown keys"_test = [] {
            expect(rejected(parse({ "--sort", "size" })));
            expect(rejected(parse({ "--sort", "CPU" })));
        };
    } };

    UTest combined{ "options combine", [] {
        auto const options{ parse({ "-s", "pid", "-i", "5" }).value() };
        expect(eq(options.interval, 5000ms));
        expect(eq(options.sort_key, SortKey::Pid));
    } };

    UTest invalid{ "unknown input", [] {
        "rejects unknown options"_test = [] {
            expect(rejected(parse({ "--verbose" })));
            expect(rejected(parse({ "-x" })));
        };
        "rejects positional arguments"_test = [] {
            expect(rejected(parse({ "extra" })));
        };
    } };

    UTest informational{ "informational options", [] {
        "help exits successfully"_test = [] {
            auto const options{ parse({ "--help" }) };
            expect(!options.has_value());
            expect(eq(options.error().code, 0));
            expect(options.error().message.empty());
        };
        "version exits successfully"_test = [] {
            auto const options{ parse({ "--version" }) };
            expect(!options.has_value());
            expect(eq(options.error().code, 0));
            expect(options.error().message.empty());
        };
    } };

} // namespace
