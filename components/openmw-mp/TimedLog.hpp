#ifndef OPENMW_LOG_HPP
#define OPENMW_LOG_HPP

#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#ifdef __GNUC__
#pragma GCC system_header
#endif

#if defined(NOLOGS)
#define LOG_INIT(logLevel)
#define LOG_QUIT()
#define LOG_MESSAGE(level, msg, ...)
#define LOG_MESSAGE_SIMPLE(level, msg, ...)
#else
#define LOG_INIT(logLevel) TimedLog::Create(logLevel)
#define LOG_QUIT() TimedLog::Delete()
#define LOG_MESSAGE(level, msg, ...) \
    TimedLog::Get().print((level), true, (__FILE__), (__LINE__), (msg) __VA_OPT__(,) __VA_ARGS__)
#define LOG_MESSAGE_SIMPLE(level, msg, ...) \
    TimedLog::Get().print((level), true, nullptr, 0, (msg) __VA_OPT__(,) __VA_ARGS__)
#define LOG_APPEND(level, msg, ...) \
    TimedLog::Get().print((level), false, nullptr, 0, (msg) __VA_OPT__(,) __VA_ARGS__)
#endif

class TimedLog
{
public:
    enum
    {
        LOG_VERBOSE = 0,
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR,
        LOG_FATAL
    };
    static void Create(int logLevel);
    static void Delete();
    static const TimedLog &Get();
    static int GetLevel();
    static void SetLevel(int level);

    template <std::size_t Size, class... Arguments>
    void print(int level, bool hasPrefix, const char* file, int line,
        const char (&message)[Size], Arguments&&... arguments) const
    {
        if (level < mLogLevel.load(std::memory_order_relaxed))
            return;

        try
        {
            boost::format formatter(message);
            (formatArgument(formatter, sanitizeArgument(std::forward<Arguments>(arguments))), ...);
            printFormatted(level, hasPrefix, file, line, formatter.str());
        }
        catch (const std::exception&)
        {
            printFormatted(level, hasPrefix, file, line, "log formatting failed");
        }
        catch (...)
        {
            printFormatted(level, hasPrefix, file, line, "log formatting failed");
        }
    }

    static std::string getFilenameTimestamp();
private:
    template <class Value>
    static auto sanitizeArgument(Value&& value)
    {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, std::string>)
            return escapeField(value);
        else if constexpr (std::is_same_v<Type, std::string_view>)
            return escapeField(value);
        else if constexpr (std::is_same_v<Type, const char*> || std::is_same_v<Type, char*>)
            return value == nullptr ? std::string("(null)") : escapeField(value);
        else if constexpr (std::is_enum_v<Type>)
            return static_cast<std::underlying_type_t<Type>>(value);
        else if constexpr (std::is_same_v<Type, char> || std::is_same_v<Type, signed char>)
            return static_cast<int>(value);
        else if constexpr (std::is_same_v<Type, unsigned char>)
            return static_cast<unsigned int>(value);
        else
            return Type(std::forward<Value>(value));
    }

    template <class Value>
    static void formatArgument(boost::format& formatter, Value&& value)
    {
        formatter % std::forward<Value>(value);
    }

    static std::string escapeField(std::string_view value);
    void printFormatted(int level, bool hasPrefix, const char* file, int line,
        std::string_view message) const;

    explicit TimedLog(int minimumLevel);
    TimedLog(const TimedLog &) = delete;
    TimedLog &operator=(TimedLog &) = delete;
    static std::unique_ptr<TimedLog> sTimedLog;
    mutable std::mutex mMutex;
    std::atomic<int> mLogLevel;
};


#endif //OPENMW_LOG_HPP
