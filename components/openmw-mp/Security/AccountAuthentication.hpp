#ifndef OPENMW_MP_SECURITY_ACCOUNT_AUTHENTICATION_HPP
#define OPENMW_MP_SECURITY_ACCOUNT_AUTHENTICATION_HPP

#include "PasswordHash.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace mwmp::security
{
    inline constexpr std::uint32_t accountLoginSchemaVersion = 1;
    inline constexpr const char* argon2idPasswordScheme = "argon2id-v1";

    struct AccountCredentials
    {
        std::uint32_t schemaVersion = 0;
        std::string passwordScheme;
        std::string passwordHash;
        std::string passwordSalt;

        bool operator==(const AccountCredentials&) const = default;
    };

    enum class AuthenticationResult
    {
        Authenticated,
        AuthenticatedMigrated,
        AuthenticatedMigrationDeferred,
        InvalidPassword,
        InvalidRecord,
        HashingFailed,
    };

    using CredentialPersistence = std::function<bool(const AccountCredentials&, std::string&)>;

    AuthenticationResult authenticateAndUpgrade(const PasswordBuffer& password,
        AccountCredentials& credentials, const CredentialPersistence& persist, std::string& error);
    bool createAccountCredentials(const PasswordBuffer& password,
        AccountCredentials& credentials, std::string& error);
    bool isAuthenticated(AuthenticationResult result) noexcept;
}

#endif
