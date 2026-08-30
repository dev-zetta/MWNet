#ifndef OPENMW_MP_SECURITY_ACCOUNT_STORE_HPP
#define OPENMW_MP_SECURITY_ACCOUNT_STORE_HPP

#include "AccountAuthentication.hpp"

#include <components/openmw-mp/Persistence/AtomicFile.hpp>

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace mwmp::security
{
    enum class AccountStoreStatus
    {
        Authenticated,
        AuthenticatedMigrated,
        AuthenticatedMigrationDeferred,
        Registered,
        InvalidCredentials,
        AccountAlreadyExists,
        InvalidAccountName,
        InvalidRecord,
        PersistenceFailed,
    };

    struct AccountStoreResult
    {
        AccountStoreStatus status = AccountStoreStatus::InvalidRecord;
        bool isNewAccount = false;
        bool legacyMaterialRemoved = false;
        std::string detail;

        bool authenticated() const noexcept;
    };

    class AccountStore
    {
    public:
        AccountStore(std::filesystem::path credentialDirectory,
            std::filesystem::path legacyPlayerDirectory,
            persistence::AtomicWriteOptions writeOptions = {});

        AccountStoreResult authenticate(std::string_view accountName,
            const PasswordBuffer& password, bool registerAccount);

    private:
        std::filesystem::path credentialPath(std::string_view canonicalName) const;
        std::filesystem::path findLegacyPlayer(std::string_view canonicalName,
            std::string& error) const;
        bool loadCredential(const std::filesystem::path& path,
            std::string_view canonicalName, AccountCredentials& credentials,
            std::string& error) const;
        bool saveCredential(const std::filesystem::path& path,
            std::string_view accountName, const AccountCredentials& credentials,
            std::string& error) const;
        bool loadLegacyCredential(const std::filesystem::path& path,
            AccountCredentials& credentials, std::string& error) const;
        bool scrubLegacyCredential(const std::filesystem::path& path,
            std::string_view accountName, std::string& error) const;

        std::filesystem::path mCredentialDirectory;
        std::filesystem::path mLegacyPlayerDirectory;
        persistence::AtomicWriteOptions mWriteOptions;
        mutable std::mutex mMutex;
    };
}

#endif
