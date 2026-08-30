#include "AccountAuthentication.hpp"

#include <exception>
#include <utility>

namespace mwmp::security
{
    namespace
    {
        bool isArgon2idRecord(const AccountCredentials& credentials) noexcept
        {
            return credentials.schemaVersion == accountLoginSchemaVersion
                && credentials.passwordScheme == argon2idPasswordScheme
                && credentials.passwordSalt.empty();
        }

        bool isLegacyRecord(const AccountCredentials& credentials) noexcept
        {
            return credentials.schemaVersion == 0 && credentials.passwordScheme.empty()
                && !credentials.passwordHash.empty() && !credentials.passwordSalt.empty();
        }
    }

    AuthenticationResult authenticateAndUpgrade(const PasswordBuffer& password,
        AccountCredentials& credentials, const CredentialPersistence& persist, std::string& error)
    {
        error.clear();
        if (isArgon2idRecord(credentials))
        {
            return PasswordHash::verifyArgon2id(password, credentials.passwordHash)
                ? AuthenticationResult::Authenticated
                : AuthenticationResult::InvalidPassword;
        }
        if (!isLegacyRecord(credentials))
        {
            error = "account login record has an unsupported credential schema";
            return AuthenticationResult::InvalidRecord;
        }
        if (!PasswordHash::verifyLegacySha256(
                password, credentials.passwordSalt, credentials.passwordHash))
            return AuthenticationResult::InvalidPassword;

        AccountCredentials upgraded;
        upgraded.schemaVersion = accountLoginSchemaVersion;
        upgraded.passwordScheme = argon2idPasswordScheme;
        if (!PasswordHash::createArgon2id(password, upgraded.passwordHash, error))
            return AuthenticationResult::HashingFailed;

        try
        {
            std::string persistenceError;
            if (!persist || !persist(upgraded, persistenceError))
            {
                error = persistenceError.empty()
                    ? "credential migration was not durably persisted"
                    : std::move(persistenceError);
                return AuthenticationResult::AuthenticatedMigrationDeferred;
            }
            credentials = std::move(upgraded);
            return AuthenticationResult::AuthenticatedMigrated;
        }
        catch (const std::exception& exception)
        {
            error = "credential migration failed: " + std::string(exception.what());
            return AuthenticationResult::AuthenticatedMigrationDeferred;
        }
        catch (...)
        {
            error = "credential migration failed with an unknown persistence error";
            return AuthenticationResult::AuthenticatedMigrationDeferred;
        }
    }

    bool createAccountCredentials(const PasswordBuffer& password,
        AccountCredentials& credentials, std::string& error)
    {
        AccountCredentials created;
        created.schemaVersion = accountLoginSchemaVersion;
        created.passwordScheme = argon2idPasswordScheme;
        if (!PasswordHash::createArgon2id(password, created.passwordHash, error))
            return false;
        credentials = std::move(created);
        return true;
    }

    bool isAuthenticated(AuthenticationResult result) noexcept
    {
        return result == AuthenticationResult::Authenticated
            || result == AuthenticationResult::AuthenticatedMigrated
            || result == AuthenticationResult::AuthenticatedMigrationDeferred;
    }
}
