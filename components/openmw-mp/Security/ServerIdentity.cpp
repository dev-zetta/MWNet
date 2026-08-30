#include "ServerIdentity.hpp"

#include "SodiumInit.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <span>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mwmp::security
{
    namespace
    {
        constexpr std::array<unsigned char, 8> sIdentityMagic{ 'T', '3', 'I', 'D', 'E', 'N', 'T', 1 };
        constexpr std::size_t sIdentityBytes = sIdentityMagic.size() + crypto_sign_SECRETKEYBYTES;

        bool hasOwnerOnlyPermissions(const std::filesystem::path& path, std::string& error)
        {
#if defined(_WIN32)
            (void)path;
            (void)error;
            return true;
#else
            struct stat status{};
            if (::stat(path.c_str(), &status) != 0)
            {
                error = "failed to inspect server identity permissions: " + std::string(std::strerror(errno));
                return false;
            }
            if ((status.st_mode & (S_IRWXG | S_IRWXO)) != 0)
            {
                error = "server identity must not be accessible by group or other users";
                return false;
            }
            return true;
#endif
        }

        bool writeAll(int descriptor, std::span<const unsigned char> bytes, std::string& error)
        {
            std::size_t written = 0;
            while (written < bytes.size())
            {
#if defined(_WIN32)
                const int result = ::_write(descriptor, bytes.data() + written,
                    static_cast<unsigned int>(bytes.size() - written));
#else
                const ssize_t result = ::write(descriptor, bytes.data() + written, bytes.size() - written);
#endif
                if (result <= 0)
                {
                    error = "failed to write server identity: " + std::string(std::strerror(errno));
                    return false;
                }
                written += static_cast<std::size_t>(result);
            }
            return true;
        }

        bool writeIdentityAtomically(const std::filesystem::path& path,
            const std::array<unsigned char, crypto_sign_SECRETKEYBYTES>& secretKey, std::string& error)
        {
            std::error_code filesystemError;
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path(), filesystemError);
            if (filesystemError)
            {
                error = "failed to create the server identity directory: " + filesystemError.message();
                return false;
            }

            std::vector<unsigned char> encoded;
            encoded.reserve(sIdentityBytes);
            encoded.insert(encoded.end(), sIdentityMagic.begin(), sIdentityMagic.end());
            encoded.insert(encoded.end(), secretKey.begin(), secretKey.end());

            std::filesystem::path temporary = path;
            temporary += ".tmp";
#if defined(_WIN32)
            const int descriptor = ::_wopen(temporary.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                _S_IREAD | _S_IWRITE);
#else
            const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#endif
            if (descriptor < 0)
            {
                error = "failed to create the temporary server identity: " + std::string(std::strerror(errno));
                return false;
            }

            bool success = writeAll(descriptor, encoded, error);
#if defined(_WIN32)
            if (success && ::_commit(descriptor) != 0)
#else
            if (success && ::fsync(descriptor) != 0)
#endif
            {
                error = "failed to flush the server identity: " + std::string(std::strerror(errno));
                success = false;
            }
#if defined(_WIN32)
            if (::_close(descriptor) != 0 && success)
#else
            if (::close(descriptor) != 0 && success)
#endif
            {
                error = "failed to close the server identity: " + std::string(std::strerror(errno));
                success = false;
            }
            sodium_memzero(encoded.data(), encoded.size());

            if (success)
            {
                std::filesystem::rename(temporary, path, filesystemError);
                if (filesystemError)
                {
                    error = "failed to install the server identity: " + filesystemError.message();
                    success = false;
                }
            }
            if (!success)
                std::filesystem::remove(temporary, filesystemError);
            return success;
        }
    }

    ServerIdentity::ServerIdentity(std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey,
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey) noexcept
        : mPublicKey(publicKey)
        , mSecretKey(secretKey)
    {
    }

    ServerIdentity::ServerIdentity(ServerIdentity&& other) noexcept
        : mPublicKey(other.mPublicKey)
        , mSecretKey(other.mSecretKey)
    {
        sodium_memzero(other.mPublicKey.data(), other.mPublicKey.size());
        sodium_memzero(other.mSecretKey.data(), other.mSecretKey.size());
    }

    ServerIdentity& ServerIdentity::operator=(ServerIdentity&& other) noexcept
    {
        if (this != &other)
        {
            sodium_memzero(mSecretKey.data(), mSecretKey.size());
            mPublicKey = other.mPublicKey;
            mSecretKey = other.mSecretKey;
            sodium_memzero(other.mPublicKey.data(), other.mPublicKey.size());
            sodium_memzero(other.mSecretKey.data(), other.mSecretKey.size());
        }
        return *this;
    }

    ServerIdentity::~ServerIdentity()
    {
        sodium_memzero(mSecretKey.data(), mSecretKey.size());
    }

    std::optional<ServerIdentity> ServerIdentity::loadOrCreate(
        const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        if (!initializeSodium(&error))
            return std::nullopt;

        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey{};
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey{};
        std::error_code filesystemError;
        if (std::filesystem::exists(path, filesystemError))
        {
            if (filesystemError || !hasOwnerOnlyPermissions(path, error))
                return std::nullopt;

            std::ifstream input(path, std::ios::binary);
            std::array<unsigned char, sIdentityBytes> encoded{};
            if (!input.read(reinterpret_cast<char*>(encoded.data()), encoded.size())
                || input.peek() != std::ifstream::traits_type::eof()
                || !std::equal(sIdentityMagic.begin(), sIdentityMagic.end(), encoded.begin()))
            {
                error = "server identity file is truncated or invalid";
                sodium_memzero(encoded.data(), encoded.size());
                return std::nullopt;
            }
            std::copy(encoded.begin() + static_cast<std::ptrdiff_t>(sIdentityMagic.size()),
                encoded.end(), secretKey.begin());
            sodium_memzero(encoded.data(), encoded.size());
            if (crypto_sign_ed25519_sk_to_pk(publicKey.data(), secretKey.data()) != 0)
            {
                error = "server identity contains an invalid Ed25519 secret key";
                sodium_memzero(secretKey.data(), secretKey.size());
                return std::nullopt;
            }
            return ServerIdentity(publicKey, secretKey);
        }
        if (filesystemError)
        {
            error = "failed to inspect the server identity path: " + filesystemError.message();
            return std::nullopt;
        }

        crypto_sign_keypair(publicKey.data(), secretKey.data());
        if (!writeIdentityAtomically(path, secretKey, error))
        {
            sodium_memzero(secretKey.data(), secretKey.size());
            return std::nullopt;
        }
        return ServerIdentity(publicKey, secretKey);
    }

    std::string ServerIdentity::fingerprint() const
    {
        std::array<char, sodium_base64_ENCODED_LEN(
                             crypto_sign_PUBLICKEYBYTES, sodium_base64_VARIANT_URLSAFE_NO_PADDING)>
            encoded{};
        sodium_bin2base64(encoded.data(), encoded.size(), mPublicKey.data(), mPublicKey.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        return "ed25519/" + std::string(encoded.data());
    }
}
