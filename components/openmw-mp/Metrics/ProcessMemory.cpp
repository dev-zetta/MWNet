#include "ProcessMemory.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace mwmp::metrics
{
    std::uint64_t residentMemoryBytes() noexcept
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(counters)))
            return 0;
        return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__APPLE__)
        mach_task_basic_info_data_t information{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&information), &count)
            != KERN_SUCCESS)
            return 0;
        return static_cast<std::uint64_t>(information.resident_size);
#elif defined(__linux__)
        std::ifstream status("/proc/self/statm");
        std::uint64_t pages = 0;
        std::uint64_t residentPages = 0;
        if (!(status >> pages >> residentPages))
            return 0;
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0)
            return 0;
        return residentPages * static_cast<std::uint64_t>(pageSize);
#else
        return 0;
#endif
    }
}
