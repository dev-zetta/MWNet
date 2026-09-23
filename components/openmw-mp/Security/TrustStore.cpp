#include "TrustStore.hpp"

#include "SodiumInit.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>

#include <sodium.h>

namespace mwmp::security
{
    namespace
    {
        constexpr std::uint32_t sSchemaVersion = 1;
        constexpr std::uintmax_t sMaximumTrustStoreBytes = 1024U * 1024U;
        constexpr std::size_t sMaximumTrustedServers = 4096;
        constexpr std::string_view sFingerprintPrefix = "ed25519/";
    }

    TrustStore::TrustStore(std::filesystem::path path) noexcept
        : mPath(std::move(path))
    {
    }

    TrustStore::TrustStore(TrustStore&& other) noexcept
    {
        std::scoped_lock lock(other.mMutex);
        mPath = std::move(other.mPath);
        mServers = std::move(other.mServers);
    }

    TrustStore& TrustStore::operator=(TrustStore&& other) noexcept
    {
        if (this != &other)
        {
            std::scoped_lock lock(mMutex, other.mMutex);
            mPath = std::move(other.mPath);
            mServers = std::move(other.mServers);
        }
        return *this;
    }

    std::optional<TrustStore> TrustStore::load(const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        if (!initializeSodium(&error))
            return std::nullopt;
        TrustStore store(path);
        std::error_code filesystemError;
        if (!std::filesystem::exists(path, filesystemError))
        {
            if (filesystemError)
            {
                error = "failed to inspect the trust store: " + filesystemError.message();
                return std::nullopt;
            }
            return store;
        }
        if (std::filesystem::file_size(path, filesystemError) > sMaximumTrustStoreBytes
            || filesystemError)
        {
            error = filesystemError ? "failed to inspect the trust store: " + filesystemError.message()
                                    : "trust store exceeds the 1 MiB limit";
            return std::nullopt;
        }

        try
        {
            boost::property_tree::ptree root;
            boost::property_tree::read_json(path.string(), root);
            if (root.get<std::uint32_t>("schemaVersion") != sSchemaVersion)
            {
                error = "unsupported trust store schema version";
                return std::nullopt;
            }
            const auto& servers = root.get_child("servers");
            for (const auto& [endpoint, value] : servers)
            {
                const std::string storedFingerprint = value.get_value<std::string>();
                if (endpoint.empty() || !isValidFingerprint(storedFingerprint)
                    || !store.mServers.emplace(endpoint, storedFingerprint).second
                    || store.mServers.size() > sMaximumTrustedServers)
                {
                    error = "trust store contains an invalid or duplicate server entry";
                    return std::nullopt;
                }
            }
            return store;
        }
        catch (const std::exception& exception)
        {
            error = "failed to parse the trust store: " + std::string(exception.what());
            return std::nullopt;
        }
    }

    TrustDecision TrustStore::assess(std::string_view host, std::uint16_t port,
        std::string_view presentedFingerprint) const
    {
        const auto endpoint = canonicalEndpoint(host, port);
        if (!endpoint)
            return TrustDecision::InvalidEndpoint;
        if (!isValidFingerprint(presentedFingerprint))
            return TrustDecision::InvalidFingerprint;

        std::scoped_lock lock(mMutex);
        const auto found = mServers.find(*endpoint);
        if (found == mServers.end())
            return TrustDecision::ConfirmationRequired;
        return found->second == presentedFingerprint ? TrustDecision::Trusted
                                                     : TrustDecision::FingerprintMismatch;
    }

    bool TrustStore::trust(std::string_view host, std::uint16_t port,
        std::string_view fingerprintValue, std::string& error)
    {
        const auto endpoint = canonicalEndpoint(host, port);
        if (!endpoint || !isValidFingerprint(fingerprintValue))
        {
            error = "invalid server endpoint or Ed25519 fingerprint";
            return false;
        }

        std::scoped_lock lock(mMutex);
        const auto found = mServers.find(*endpoint);
        if (found != mServers.end() && found->second != fingerprintValue)
        {
            error = "the server fingerprint does not match the existing trust record";
            return false;
        }
        if (found != mServers.end())
            return true;
        if (mServers.size() >= sMaximumTrustedServers)
        {
            error = "trust store has reached its server limit";
            return false;
        }

        mServers.emplace(*endpoint, fingerprintValue);
        if (!save(error))
        {
            mServers.erase(*endpoint);
            return false;
        }
        return true;
    }

    std::optional<std::string> TrustStore::trustedFingerprint(
        std::string_view host, std::uint16_t port) const
    {
        const auto endpoint = canonicalEndpoint(host, port);
        if (!endpoint)
            return std::nullopt;
        std::scoped_lock lock(mMutex);
        const auto found = mServers.find(*endpoint);
        return found == mServers.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<std::string> TrustStore::canonicalEndpoint(std::string_view host, std::uint16_t port)
    {
        if (host.empty() || host.size() > 253 || port == 0)
            return std::nullopt;
        if (host.front() == '[' && host.back() == ']' && host.size() > 2)
            host = host.substr(1, host.size() - 2);
        while (host.size() > 1 && host.back() == '.')
            host.remove_suffix(1);

        std::string canonical;
        canonical.reserve(host.size() + 8);
        bool ipv6 = false;
        for (const unsigned char character : host)
        {
            if (character == ':')
                ipv6 = true;
            if (!(std::isalnum(character) || character == '.' || character == '-'
                    || character == '_' || character == ':' || character == '%'))
                return std::nullopt;
            canonical.push_back(static_cast<char>(std::tolower(character)));
        }
        if (canonical.empty())
            return std::nullopt;
        if (ipv6)
            canonical = '[' + canonical + ']';
        canonical += ':' + std::to_string(port);
        return canonical;
    }

    bool TrustStore::isValidFingerprint(std::string_view value) noexcept
    {
        if (!initializeSodium())
            return false;
        if (!value.starts_with(sFingerprintPrefix))
            return false;
        value.remove_prefix(sFingerprintPrefix.size());
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> decoded{};
        std::size_t decodedBytes = 0;
        return sodium_base642bin(decoded.data(), decoded.size(), value.data(), value.size(), nullptr,
                   &decodedBytes, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING)
                == 0
            && decodedBytes == decoded.size();
    }

    bool TrustStore::save(std::string& error) const
    {
        if (mPath.empty())
            return true;
        try
        {
            boost::property_tree::ptree root;
            boost::property_tree::ptree servers;
            root.put("schemaVersion", sSchemaVersion);
            for (const auto& [endpoint, fingerprintValue] : mServers)
            {
                boost::property_tree::ptree value;
                value.put_value(fingerprintValue);
                servers.push_back({ endpoint, value });
            }
            root.add_child("servers", servers);

            std::error_code filesystemError;
            if (!mPath.parent_path().empty())
                std::filesystem::create_directories(mPath.parent_path(), filesystemError);
            if (filesystemError)
            {
                error = "failed to create the trust store directory: " + filesystemError.message();
                return false;
            }

            std::filesystem::path temporary = mPath;
            temporary += ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                boost::property_tree::write_json(output, root, false);
                output.flush();
                if (!output)
                {
                    error = "failed to flush the temporary trust store";
                    return false;
                }
            }
            std::filesystem::permissions(temporary,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace, filesystemError);
            if (filesystemError)
            {
                error = "failed to restrict trust store permissions: " + filesystemError.message();
                std::filesystem::remove(temporary, filesystemError);
                return false;
            }
            std::filesystem::rename(temporary, mPath, filesystemError);
            if (filesystemError)
            {
                error = "failed to atomically replace the trust store: " + filesystemError.message();
                std::filesystem::remove(temporary, filesystemError);
                return false;
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            error = "failed to save the trust store: " + std::string(exception.what());
            return false;
        }
    }
}
