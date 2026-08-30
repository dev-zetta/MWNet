#include "SodiumInit.hpp"

#include <mutex>

#include <sodium.h>

namespace mwmp::security
{
    bool initializeSodium(std::string* error) noexcept
    {
        static std::once_flag once;
        static bool initialized = false;
        std::call_once(once, [] { initialized = sodium_init() >= 0; });
        if (!initialized && error)
            *error = "libsodium initialization failed";
        return initialized;
    }
}
