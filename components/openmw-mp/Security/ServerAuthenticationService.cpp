#include "ServerAuthenticationService.hpp"

#include <sodium.h>

#include <utility>

namespace mwmp::security
{
    ServerAuthenticationService::ServerAuthenticationService(
        std::filesystem::path credentialDirectory,
        std::filesystem::path legacyPlayerDirectory, AuthenticationLimits limits)
        : mAccounts(std::move(credentialDirectory), std::move(legacyPlayerDirectory))
        , mLimiter(limits)
    {
    }

    bool ServerAuthenticationService::setAccessPasswordHash(
        std::string encodedHash, std::string& error)
    {
        error.clear();
        if (encodedHash.empty())
        {
            mAccessPasswordHash.clear();
            return true;
        }
        if (!encodedHash.starts_with("$argon2id$")
            || encodedHash.size() >= crypto_pwhash_STRBYTES)
        {
            error = "server access passwordHash must be a libsodium Argon2id encoded hash";
            return false;
        }
        mAccessPasswordHash = std::move(encodedHash);
        return true;
    }

    ServerAuthenticationResult ServerAuthenticationService::authenticate(
        AuthenticationRequest request, std::string_view address,
        AuthenticationRateLimiter::Clock::time_point now)
    {
        ServerAuthenticationResult result;
        const auto reject = [&](AuthenticationResponseStatus status, const char* message) {
            result.response.status = status;
            result.response.message = message;
            return std::move(result);
        };

        if (!request.password)
            return reject(AuthenticationResponseStatus::InvalidCredentials,
                "Invalid account name or password.");
        switch (mLimiter.begin(request.accountName, address, now))
        {
            case AuthenticationAttemptDecision::Allowed:
                break;
            case AuthenticationAttemptDecision::AccountAndAddressLocked:
            case AuthenticationAttemptDecision::PreKdfRateLimited:
            case AuthenticationAttemptDecision::CapacityReached:
                return reject(AuthenticationResponseStatus::RateLimited,
                    "Authentication is temporarily rate limited.");
            case AuthenticationAttemptDecision::InvalidIdentity:
                return reject(AuthenticationResponseStatus::InvalidCredentials,
                    "Invalid account name or password.");
        }

        if (!mAccessPasswordHash.empty()
            && (!request.serverAccessPassword
                || !PasswordHash::verifyArgon2id(
                    *request.serverAccessPassword, mAccessPasswordHash)))
        {
            mLimiter.recordFailure(request.accountName, address, now);
            return reject(AuthenticationResponseStatus::ServerAccessDenied,
                "Invalid server access password.");
        }

        const bool registration
            = request.operation == AuthenticationOperation::Register;
        auto account = mAccounts.authenticate(
            request.accountName, *request.password, registration);
        if (!account.authenticated())
        {
            mLimiter.recordFailure(request.accountName, address, now);
            if (account.status == AccountStoreStatus::PersistenceFailed
                || account.status == AccountStoreStatus::InvalidRecord)
                return reject(AuthenticationResponseStatus::Rejected,
                    "The server could not complete authentication.");
            return reject(AuthenticationResponseStatus::InvalidCredentials,
                "Invalid account name or password.");
        }

        mLimiter.recordSuccess(request.accountName, address);
        result.accountName = std::move(request.accountName);
        result.isNewAccount = account.isNewAccount;
        result.legacyMaterialRemoved = account.legacyMaterialRemoved;
        result.response.status = account.isNewAccount
            ? AuthenticationResponseStatus::Registered
            : AuthenticationResponseStatus::Authenticated;
        result.response.message = account.isNewAccount
            ? "Account registered." : "Account authenticated.";
        return result;
    }
}
