#ifndef OPENMW_MP_SECURITY_SERVER_IDENTITY_HPP
#define OPENMW_MP_SECURITY_SERVER_IDENTITY_HPP

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <span>

#include <sodium.h>

namespace mwmp::security
{
    class ServerIdentity
    {
    public:
        ServerIdentity(ServerIdentity&& other) noexcept;
        ServerIdentity& operator=(ServerIdentity&& other) noexcept;
        ~ServerIdentity();

        ServerIdentity(const ServerIdentity&) = delete;
        ServerIdentity& operator=(const ServerIdentity&) = delete;

        static std::optional<ServerIdentity> loadOrCreate(
            const std::filesystem::path& path, std::string& error);

        const std::array<unsigned char, crypto_sign_PUBLICKEYBYTES>& publicKey() const noexcept
        {
            return mPublicKey;
        }
        const std::array<unsigned char, crypto_sign_SECRETKEYBYTES>& secretKey() const noexcept
        {
            return mSecretKey;
        }
        std::string fingerprint() const;
        std::array<unsigned char, crypto_sign_BYTES> signDiscovery(
            std::span<const unsigned char> payload) const;

    private:
        ServerIdentity(std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey,
            std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey) noexcept;

        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> mPublicKey{};
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> mSecretKey{};
    };
}

#endif
