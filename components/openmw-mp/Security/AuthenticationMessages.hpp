#ifndef OPENMW_MP_SECURITY_AUTHENTICATION_MESSAGES_HPP
#define OPENMW_MP_SECURITY_AUTHENTICATION_MESSAGES_HPP

#include "PasswordHash.hpp"

#include <components/openmw-mp/Protocol/PacketCodec.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp::security
{
    inline constexpr std::uint32_t authenticationMessageSchemaVersion = 1;

    enum class AuthenticationOperation : std::uint8_t
    {
        Login = 1,
        Register = 2,
    };

    struct AuthenticationRequest
    {
        AuthenticationOperation operation = AuthenticationOperation::Login;
        std::string accountName;
        std::optional<PasswordBuffer> password;
        std::optional<PasswordBuffer> serverAccessPassword;
    };

    enum class AuthenticationResponseStatus : std::uint8_t
    {
        Authenticated = 1,
        Registered = 2,
        InvalidCredentials = 3,
        RateLimited = 4,
        ServerAccessDenied = 5,
        Rejected = 6,
    };

    struct AuthenticationResponse
    {
        AuthenticationResponseStatus status = AuthenticationResponseStatus::Rejected;
        std::string message;

        bool authenticated() const noexcept
        {
            return status == AuthenticationResponseStatus::Authenticated
                || status == AuthenticationResponseStatus::Registered;
        }
    };

    bool encodeAuthenticationRequest(AuthenticationOperation operation,
        std::string_view accountName, const PasswordBuffer& password,
        const PasswordBuffer* serverAccessPassword, std::vector<std::byte>& encoded,
        protocol::CodecError& error);
    protocol::DecodeResult decodeAuthenticationRequest(
        std::span<const std::byte> encoded, AuthenticationRequest& request);

    bool encodeAuthenticationResponse(const AuthenticationResponse& response,
        std::vector<std::byte>& encoded, protocol::CodecError& error);
    protocol::DecodeResult decodeAuthenticationResponse(
        std::span<const std::byte> encoded, AuthenticationResponse& response);
}

#endif
