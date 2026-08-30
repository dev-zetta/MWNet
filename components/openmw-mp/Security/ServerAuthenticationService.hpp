#ifndef OPENMW_MP_SECURITY_SERVER_AUTHENTICATION_SERVICE_HPP
#define OPENMW_MP_SECURITY_SERVER_AUTHENTICATION_SERVICE_HPP

#include "AccountStore.hpp"
#include "AuthenticationMessages.hpp"
#include "AuthenticationRateLimiter.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace mwmp::security
{
    struct ServerAuthenticationResult
    {
        AuthenticationResponse response;
        std::string accountName;
        bool isNewAccount = false;
        bool legacyMaterialRemoved = false;
    };

    class ServerAuthenticationService
    {
    public:
        ServerAuthenticationService(std::filesystem::path credentialDirectory,
            std::filesystem::path legacyPlayerDirectory,
            AuthenticationLimits limits = {});

        bool setAccessPasswordHash(std::string encodedHash, std::string& error);
        bool requiresAccessPassword() const noexcept { return !mAccessPasswordHash.empty(); }

        ServerAuthenticationResult authenticate(AuthenticationRequest request,
            std::string_view address,
            AuthenticationRateLimiter::Clock::time_point now
                = AuthenticationRateLimiter::Clock::now());

    private:
        AccountStore mAccounts;
        AuthenticationRateLimiter mLimiter;
        std::string mAccessPasswordHash;
    };
}

#endif
