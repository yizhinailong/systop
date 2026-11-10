import std;
import mcpplibs.mtest;
import systop;

namespace {

    using mcpplibs::mtest::UTest;
    using mcpplibs::mtest::eq;
    using mcpplibs::mtest::expect;

    auto filled(std::size_t count) -> monitor::History {
        monitor::History history;
        for (std::size_t index{ 0 }; index < count; ++index) {
            history.Push(static_cast<double>(index));
        }
        return history;
    }

    UTest empty{ "a new history is empty", [] {
        expect(monitor::History{}.Values().empty());
    } };

    UTest order{ "values keep their push order", [] {
        expect(eq(filled(3).Values(), std::deque{ 0.0, 1.0, 2.0 }));
    } };

    UTest capacity{ "a full history drops its oldest values", [] {
        auto const history{ filled(monitor::HISTORY_CAPACITY + 2) };
        auto const& values{ history.Values() };
        expect(eq(values.size(), monitor::HISTORY_CAPACITY));
        expect(eq(values.front(), 2.0));
        expect(eq(values.back(), static_cast<double>(monitor::HISTORY_CAPACITY + 1)));
    } };

} // namespace
