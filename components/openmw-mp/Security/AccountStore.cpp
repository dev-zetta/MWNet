#include "AccountStore.hpp"

#include "SodiumInit.hpp"

#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include <sodium.h>

namespace mwmp::security
{
    namespace
    {
        constexpr std::uintmax_t sMaximumAccountRecordBytes = 1024U * 1024U;
        constexpr std::uintmax_t sMaximumLegacyPlayerBytes = 16U * 1024U * 1024U;
        constexpr std::size_t sMaximumLegacyPlayers = 4096;

        std::string canonicalName(std::string_view value)
        {
            std::string canonical(value);
            std::ranges::transform(canonical, canonical.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return canonical;
        }

        bool validAccountName(std::string_view value)
        {
            if (value.empty())
                return false;
            protocol::PacketWriter writer;
            if (!writer.writeString(value, protocol::limits::accountNameBytes))
                return false;
            return std::ranges::none_of(value, [](unsigned char character) {
                return character < 0x20U || character == 0x7fU;
            });
        }

        bool readFile(const std::filesystem::path& path, std::uintmax_t maximumBytes,
            std::string& contents, std::string& error)
        {
            std::error_code filesystemError;
            const auto size = std::filesystem::file_size(path, filesystemError);
            if (filesystemError || size > maximumBytes)
            {
                error = filesystemError ? "failed to inspect account record: "
                        + filesystemError.message() : "account record exceeds its size limit";
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                error = "failed to open account record";
                return false;
            }
            contents.resize(static_cast<std::size_t>(size));
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!input && !contents.empty())
            {
                error = "failed to read account record";
                return false;
            }
            return true;
        }

        std::string jsonEscape(std::string_view value)
        {
            constexpr char hexadecimal[] = "0123456789abcdef";
            std::string escaped;
            escaped.reserve(value.size());
            for (const unsigned char character : value)
            {
                switch (character)
                {
                    case '"': escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\t': escaped += "\\t"; break;
                    default:
                        if (character < 0x20U)
                        {
                            escaped += "\\u00";
                            escaped.push_back(hexadecimal[character >> 4U]);
                            escaped.push_back(hexadecimal[character & 0x0fU]);
                        }
                        else
                            escaped.push_back(static_cast<char>(character));
                }
            }
            return escaped;
        }

        void skipWhitespace(std::string_view json, std::size_t& position)
        {
            while (position < json.size()
                && std::isspace(static_cast<unsigned char>(json[position])))
                ++position;
        }

        bool skipString(std::string_view json, std::size_t& position)
        {
            if (position >= json.size() || json[position++] != '"')
                return false;
            while (position < json.size())
            {
                const char character = json[position++];
                if (character == '"')
                    return true;
                if (character == '\\')
                {
                    if (position >= json.size())
                        return false;
                    if (json[position++] == 'u')
                    {
                        if (json.size() - position < 4)
                            return false;
                        position += 4;
                    }
                }
                else if (static_cast<unsigned char>(character) < 0x20U)
                    return false;
            }
            return false;
        }

        bool skipValue(std::string_view json, std::size_t& position)
        {
            skipWhitespace(json, position);
            if (position >= json.size())
                return false;
            if (json[position] == '"')
                return skipString(json, position);
            if (json[position] != '{' && json[position] != '[')
            {
                const std::size_t start = position;
                while (position < json.size() && json[position] != ','
                    && json[position] != '}' && json[position] != ']')
                    ++position;
                return position > start;
            }

            const char opening = json[position++];
            const char closing = opening == '{' ? '}' : ']';
            std::size_t depth = 1;
            while (position < json.size() && depth != 0)
            {
                if (json[position] == '"')
                {
                    if (!skipString(json, position))
                        return false;
                }
                else
                {
                    if (json[position] == opening)
                        ++depth;
                    else if (json[position] == closing)
                        --depth;
                    ++position;
                }
            }
            return depth == 0;
        }

        bool replaceTopLevelLogin(std::string& json, std::string_view replacement,
            bool& changed)
        {
            changed = false;
            std::size_t position = 0;
            skipWhitespace(json, position);
            if (position >= json.size() || json[position++] != '{')
                return false;
            for (;;)
            {
                skipWhitespace(json, position);
                if (position >= json.size() || json[position] == '}')
                    return false;
                const std::size_t keyStart = position;
                if (!skipString(json, position))
                    return false;
                const std::size_t keyEnd = position;
                skipWhitespace(json, position);
                if (position >= json.size() || json[position++] != ':')
                    return false;
                skipWhitespace(json, position);
                const std::size_t valueStart = position;
                if (!skipValue(json, position))
                    return false;
                const std::size_t valueEnd = position;
                if (json.substr(keyStart, keyEnd - keyStart) == "\"login\"")
                {
                    if (json.compare(valueStart, valueEnd - valueStart, replacement) == 0)
                        return true;
                    json.replace(valueStart, valueEnd - valueStart, replacement);
                    changed = true;
                    return true;
                }
                skipWhitespace(json, position);
                if (position >= json.size() || json[position++] != ',')
                    return false;
            }
        }

        std::span<const std::byte> bytes(const std::string& value)
        {
            return std::as_bytes(std::span(value));
        }
    }

    bool AccountStoreResult::authenticated() const noexcept
    {
        return status == AccountStoreStatus::Authenticated
            || status == AccountStoreStatus::AuthenticatedMigrated
            || status == AccountStoreStatus::AuthenticatedMigrationDeferred
            || status == AccountStoreStatus::Registered;
    }

    AccountStore::AccountStore(std::filesystem::path credentialDirectory,
        std::filesystem::path legacyPlayerDirectory,
        persistence::AtomicWriteOptions writeOptions)
        : mCredentialDirectory(std::move(credentialDirectory))
        , mLegacyPlayerDirectory(std::move(legacyPlayerDirectory))
        , mWriteOptions(std::move(writeOptions))
    {
        mWriteOptions.backup = persistence::BackupPolicy::None;
        mWriteOptions.ownerOnly = true;
        mWriteOptions.maximumBytes = sMaximumAccountRecordBytes;
    }

    AccountStoreResult AccountStore::authenticate(std::string_view accountName,
        const PasswordBuffer& password, bool registerAccount)
    {
        std::scoped_lock lock(mMutex);
        std::string initializationError;
        if (!initializeSodium(&initializationError))
            return { AccountStoreStatus::PersistenceFailed, false, false,
                std::move(initializationError) };
        if (!validAccountName(accountName))
            return { AccountStoreStatus::InvalidAccountName, false, false,
                "invalid account name" };
        const std::string canonical = canonicalName(accountName);
        const auto storedPath = credentialPath(canonical);

        std::error_code filesystemError;
        if (std::filesystem::exists(storedPath, filesystemError))
        {
            if (registerAccount)
                return { AccountStoreStatus::AccountAlreadyExists, false, false,
                    "account already exists" };
            AccountCredentials credentials;
            std::string error;
            if (!loadCredential(storedPath, canonical, credentials, error))
                return { AccountStoreStatus::InvalidRecord, false, false, std::move(error) };
            const auto result = authenticateAndUpgrade(password, credentials,
                [&](const AccountCredentials& upgraded, std::string& persistenceError) {
                    return saveCredential(storedPath, accountName, upgraded, persistenceError);
                }, error);
            if (!isAuthenticated(result))
                return { result == AuthenticationResult::InvalidPassword
                        ? AccountStoreStatus::InvalidCredentials
                        : AccountStoreStatus::InvalidRecord,
                    false, false, std::move(error) };
            std::string legacyError;
            const auto legacyPath = findLegacyPlayer(canonical, legacyError);
            bool scrubbed = legacyPath.empty();
            if (!legacyPath.empty())
                scrubbed = scrubLegacyCredential(legacyPath, accountName, legacyError);
            return { result == AuthenticationResult::AuthenticatedMigrationDeferred
                    ? AccountStoreStatus::AuthenticatedMigrationDeferred
                    : result == AuthenticationResult::AuthenticatedMigrated
                        ? AccountStoreStatus::AuthenticatedMigrated
                        : AccountStoreStatus::Authenticated,
                false, scrubbed, legacyError.empty() ? std::move(error)
                                                     : std::move(legacyError) };
        }
        if (filesystemError)
            return { AccountStoreStatus::PersistenceFailed, false, false,
                "failed to inspect credential store: " + filesystemError.message() };

        std::string error;
        const auto legacyPath = findLegacyPlayer(canonical, error);
        if (!error.empty())
            return { AccountStoreStatus::InvalidRecord, false, false, std::move(error) };
        if (!legacyPath.empty())
        {
            if (registerAccount)
                return { AccountStoreStatus::AccountAlreadyExists, false, false,
                    "account already exists" };
            AccountCredentials credentials;
            if (!loadLegacyCredential(legacyPath, credentials, error))
                return { AccountStoreStatus::InvalidRecord, false, false, std::move(error) };
            const auto result = authenticateAndUpgrade(password, credentials,
                [&](const AccountCredentials& upgraded, std::string& persistenceError) {
                    return saveCredential(storedPath, accountName, upgraded, persistenceError);
                }, error);
            if (!isAuthenticated(result))
                return { result == AuthenticationResult::InvalidPassword
                        ? AccountStoreStatus::InvalidCredentials
                        : AccountStoreStatus::InvalidRecord,
                    false, false, std::move(error) };
            if (result == AuthenticationResult::AuthenticatedMigrationDeferred)
                return { AccountStoreStatus::AuthenticatedMigrationDeferred,
                    false, false, std::move(error) };

            std::string scrubError;
            const bool scrubbed = scrubLegacyCredential(legacyPath, accountName, scrubError);
            return { AccountStoreStatus::AuthenticatedMigrated, false, scrubbed,
                scrubbed ? std::string{} : std::move(scrubError) };
        }

        if (!registerAccount)
            return { AccountStoreStatus::InvalidCredentials, false, false,
                "invalid account name or password" };
        AccountCredentials credentials;
        if (!createAccountCredentials(password, credentials, error)
            || !saveCredential(storedPath, accountName, credentials, error))
            return { AccountStoreStatus::PersistenceFailed, false, false, std::move(error) };
        return { AccountStoreStatus::Registered, true, false, {} };
    }

    std::filesystem::path AccountStore::credentialPath(std::string_view canonical) const
    {
        std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
        crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(canonical.data()),
            canonical.size());
        std::array<char, crypto_hash_sha256_BYTES * 2U + 1U> hexadecimal{};
        sodium_bin2hex(hexadecimal.data(), hexadecimal.size(), digest.data(), digest.size());
        return mCredentialDirectory / (std::string(hexadecimal.data()) + ".json");
    }

    std::filesystem::path AccountStore::findLegacyPlayer(
        std::string_view canonical, std::string& error) const
    {
        error.clear();
        std::error_code filesystemError;
        if (!std::filesystem::exists(mLegacyPlayerDirectory, filesystemError))
            return {};
        if (filesystemError)
        {
            error = "failed to inspect legacy account directory: " + filesystemError.message();
            return {};
        }
        std::size_t entries = 0;
        std::filesystem::directory_iterator iterator(mLegacyPlayerDirectory, filesystemError);
        const std::filesystem::directory_iterator end;
        for (; iterator != end; iterator.increment(filesystemError))
        {
            if (filesystemError)
            {
                error = "failed to enumerate legacy account directory: "
                    + filesystemError.message();
                return {};
            }
            if (++entries > sMaximumLegacyPlayers)
            {
                error = "legacy account directory exceeds its entry limit";
                return {};
            }
            const auto& entry = *iterator;
            if (entry.is_regular_file(filesystemError)
                && entry.path().extension() == ".json"
                && canonicalName(entry.path().stem().string()) == canonical)
                return entry.path();
            if (filesystemError)
            {
                error = "failed to inspect legacy account entry: "
                    + filesystemError.message();
                return {};
            }
        }
        return {};
    }

    bool AccountStore::loadCredential(const std::filesystem::path& path,
        std::string_view canonical, AccountCredentials& credentials, std::string& error) const
    {
        try
        {
            std::string contents;
            if (!readFile(path, sMaximumAccountRecordBytes, contents, error))
                return false;
            std::istringstream input(contents);
            boost::property_tree::ptree root;
            boost::property_tree::read_json(input, root);
            if (canonicalName(root.get<std::string>("login.name")) != canonical)
            {
                error = "credential record account name mismatch";
                return false;
            }
            AccountCredentials loaded;
            loaded.schemaVersion = root.get<std::uint32_t>("login.schemaVersion");
            loaded.passwordScheme = root.get<std::string>("login.passwordScheme");
            loaded.passwordHash = root.get<std::string>("login.passwordHash");
            loaded.passwordSalt = root.get<std::string>("login.passwordSalt", "");
            credentials = std::move(loaded);
            return true;
        }
        catch (const std::exception& exception)
        {
            error = "failed to parse credential record: " + std::string(exception.what());
            return false;
        }
    }

    bool AccountStore::saveCredential(const std::filesystem::path& path,
        std::string_view accountName, const AccountCredentials& credentials,
        std::string& error) const
    {
        std::string contents = "{\"login\":{\"name\":\"" + jsonEscape(accountName)
            + "\",\"schemaVersion\":" + std::to_string(credentials.schemaVersion)
            + ",\"passwordScheme\":\"" + jsonEscape(credentials.passwordScheme)
            + "\",\"passwordHash\":\"" + jsonEscape(credentials.passwordHash) + "\"";
        if (!credentials.passwordSalt.empty())
            contents += ",\"passwordSalt\":\"" + jsonEscape(credentials.passwordSalt) + "\"";
        contents += "}}\n";
        return persistence::writeFileAtomically(path, bytes(contents), mWriteOptions, error);
    }

    bool AccountStore::loadLegacyCredential(const std::filesystem::path& path,
        AccountCredentials& credentials, std::string& error) const
    {
        try
        {
            std::string contents;
            if (!readFile(path, sMaximumLegacyPlayerBytes, contents, error))
                return false;
            std::istringstream input(contents);
            boost::property_tree::ptree root;
            boost::property_tree::read_json(input, root);
            AccountCredentials loaded;
            loaded.passwordHash = root.get<std::string>("login.passwordHash");
            loaded.passwordSalt = root.get<std::string>("login.passwordSalt");
            credentials = std::move(loaded);
            return true;
        }
        catch (const std::exception& exception)
        {
            error = "failed to parse legacy account record: " + std::string(exception.what());
            return false;
        }
    }

    bool AccountStore::scrubLegacyCredential(const std::filesystem::path& path,
        std::string_view accountName, std::string& error) const
    {
        std::string contents;
        if (!readFile(path, sMaximumLegacyPlayerBytes, contents, error))
            return false;
        const std::string replacement = "{\"name\":\"" + jsonEscape(accountName)
            + "\",\"schemaVersion\":1,\"passwordScheme\":\"argon2id-v1\"}";
        bool changed = false;
        if (!replaceTopLevelLogin(contents, replacement, changed))
        {
            error = "legacy account record has no valid top-level login object";
            return false;
        }
        // Migrated accounts are checked on every login. Avoid another durable
        // write when the legacy credential material has already been removed.
        if (!changed)
            return true;
        auto options = mWriteOptions;
        options.maximumBytes = sMaximumLegacyPlayerBytes;
        return persistence::writeFileAtomically(path, bytes(contents), options, error);
    }
}
