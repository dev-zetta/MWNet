#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

#include "TimedLog.hpp"

std::unique_ptr<TimedLog> TimedLog::sTimedLog;

TimedLog::TimedLog(int minimumLevel)
    : mLogLevel(minimumLevel)
{

}

void TimedLog::Create(int logLevel)
{
    if (sTimedLog)
        return;
    sTimedLog.reset(new TimedLog(logLevel));
}

void TimedLog::Delete()
{
    sTimedLog.reset();
}

const TimedLog &TimedLog::Get()
{
    return *sTimedLog;
}

int TimedLog::GetLevel()
{
    return sTimedLog->mLogLevel.load(std::memory_order_relaxed);
}

void TimedLog::SetLevel(int level)
{
    sTimedLog->mLogLevel.store(level, std::memory_order_relaxed);
}

namespace
{
    std::string getTime()
    {
        const std::time_t time = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        std::array<char, 20> result{};
        std::strftime(result.data(), result.size(), "%Y-%m-%d %H:%M:%S", &local);
        return result.data();
    }
}

std::string TimedLog::escapeField(std::string_view value)
{
    constexpr std::size_t maximumFieldBytes = 4096;
    std::ostringstream escaped;
    const std::size_t length = std::min(value.size(), maximumFieldBytes);
    for (std::size_t index = 0; index < length; ++index)
    {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        switch (character)
        {
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                if (character < 0x20 || character == 0x7f)
                {
                    escaped << "\\x" << std::hex << std::uppercase << std::setw(2)
                            << std::setfill('0') << static_cast<unsigned int>(character)
                            << std::dec;
                }
                else
                    escaped << static_cast<char>(character);
        }
    }
    if (value.size() > maximumFieldBytes)
        escaped << "...";
    return escaped.str();
}

void TimedLog::printFormatted(int level, bool hasPrefix, const char* file, int line,
    std::string_view message) const
{
    std::lock_guard lock(mMutex);
    std::ostringstream output;

    if (hasPrefix)
    {
        output << "[" << getTime() << "] ";

        if (file != nullptr && line != 0)
        {
            output << "[" << file << ":" << line << "] ";
        }

        output << "[";
        switch (level)
        {
            case LOG_WARN:
                output << "WARN";
                break;
            case LOG_ERROR:
                output << "ERR";
                break;
            case LOG_FATAL:
                output << "FATAL";
                break;
            default:
                output << "INFO";
        }
        output << "]: ";
    }

    output << message;
    if (message.empty() || message.back() != '\n')
        output << '\n';
    std::cout << output.str() << std::flush;
}

std::string TimedLog::getFilenameTimestamp()
{
    const std::time_t rawtime = std::time(nullptr);
    std::tm timeinfo{};
#ifdef _WIN32
    localtime_s(&timeinfo, &rawtime);
#else
    localtime_r(&rawtime, &timeinfo);
#endif
    char buffer[25]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H_%M_%S", &timeinfo);
    std::string timestamp(buffer);
    return timestamp;
}
