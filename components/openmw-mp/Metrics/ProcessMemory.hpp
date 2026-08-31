#ifndef OPENMW_MP_METRICS_PROCESS_MEMORY_HPP
#define OPENMW_MP_METRICS_PROCESS_MEMORY_HPP

#include <cstdint>

namespace mwmp::metrics
{
    std::uint64_t residentMemoryBytes() noexcept;
}

#endif
