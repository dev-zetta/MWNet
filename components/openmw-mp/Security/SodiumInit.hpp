#ifndef OPENMW_MP_SECURITY_SODIUM_INIT_HPP
#define OPENMW_MP_SECURITY_SODIUM_INIT_HPP

#include <string>

namespace mwmp::security
{
    bool initializeSodium(std::string* error = nullptr) noexcept;
}

#endif
