// The small test harness of the host tests of the pure Forever rules (src/game/Forever/tests/*Test.cpp).
// No GoogleTest and no core: wowai/tests/forever-rules.test.sh builds each test file alone with the host
// compiler and runs it. The glob of src/game/Forever does not take this subfolder into the core.
//
// A test file includes this header, calls Check, Equal and Near in main, and ends main with
// `return ForeverTest::Summary();`.

#ifndef WOWAI_GAME_FOREVER_TEST_CHECK_H
#define WOWAI_GAME_FOREVER_TEST_CHECK_H

#include <cmath>
#include <cstdio>

namespace ForeverTest
{
    struct Counts
    {
        int checks = 0;
        int failures = 0;
    };

    inline Counts& Tally()
    {
        static Counts counts;
        return counts;
    }

    inline void Check(char const* name, bool ok)
    {
        ++Tally().checks;
        if (!ok)
            ++Tally().failures;
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    }

    inline void Equal(char const* name, long long actual, long long expected)
    {
        Check(name, actual == expected);
        if (actual != expected)
            std::printf("      %lld, expected %lld\n", actual, expected);
    }

    inline void Near(char const* name, float actual, float expected)
    {
        bool const ok = std::fabs(actual - expected) <= 0.001f;
        Check(name, ok);
        if (!ok)
            std::printf("      %.4f, expected %.4f\n", actual, expected);
    }

    // Prints the count and returns the exit code of the test: 0 when every check passed.
    inline int Summary()
    {
        std::printf("%d of %d checks passed.\n", Tally().checks - Tally().failures, Tally().checks);
        return Tally().failures == 0 ? 0 : 1;
    }
}

#endif
