#include <components/openmw-mp/TimedLog.hpp>

#include <iostream>
#include <sstream>
#include <string>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << "timed_log.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testTypedFormattingAndEscaping()
    {
        std::ostringstream output;
        std::streambuf* original = std::cout.rdbuf(output.rdbuf());
        TimedLog::Create(TimedLog::LOG_VERBOSE);
        LOG_APPEND(TimedLog::LOG_INFO, "player=%s count=%i", "name%s\nnext", 7);
        TimedLog::Delete();
        std::cout.rdbuf(original);

        EXPECT(output.str() == "player=name%s\\nnext count=7\n");
    }
}

int runTimedLogTests()
{
    testTypedFormattingAndEscaping();
    return failures;
}
