#ifndef OPENMW_MP_SECURITY_TRUST_STORE_HPP
#define OPENMW_MP_SECURITY_TRUST_STORE_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace mwmp::security
{
    enum class TrustDecision
    {
        Trusted,
        ConfirmationRequired,
        FingerprintMismatch,
        InvalidEndpoint,
        InvalidFingerprint,
    };

    class TrustStore
    {
    public:
        static std::optional<TrustStore> load(const std::filesystem::path& path, std::string& error);

        TrustStore(TrustStore&& other) noexcept;
        TrustStore& operator=(TrustStore&& other) noexcept;
        TrustStore(const TrustStore&) = delete;
        TrustStore& operator=(const TrustStore&) = delete;

        TrustDecision assess(std::string_view host, std::uint16_t port,
            std::string_view presentedFingerprint) const;
        bool trust(std::string_view host, std::uint16_t port,
            std::string_view fingerprint, std::string& error);
        std::optional<std::string> trustedFingerprint(std::string_view host, std::uint16_t port) const;

        static std::optional<std::string> canonicalEndpoint(std::string_view host, std::uint16_t port);
        static bool isValidFingerprint(std::string_view fingerprint) noexcept;

    private:
        explicit TrustStore(std::filesystem::path path) noexcept;
        bool save(std::string& error) const;

        std::filesystem::path mPath;
        mutable std::mutex mMutex;
        std::map<std::string, std::string> mServers;
    };
}

#endif
