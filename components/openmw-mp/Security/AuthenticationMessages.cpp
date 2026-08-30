#include "AuthenticationMessages.hpp"

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <sodium.h>

namespace mwmp::security
{
    namespace
    {
        constexpr std::size_t sMaximumResponseBytes = 512;

        std::string_view view(const PasswordBuffer& password) noexcept
        {
            const auto characters = password.characters();
            return { characters.data(), characters.size() };
        }

        bool validOperation(std::uint8_t value) noexcept
        {
            return value == static_cast<std::uint8_t>(AuthenticationOperation::Login)
                || value == static_cast<std::uint8_t>(AuthenticationOperation::Register);
        }

        bool validResponseStatus(std::uint8_t value) noexcept
        {
            return value >= static_cast<std::uint8_t>(AuthenticationResponseStatus::Authenticated)
                && value <= static_cast<std::uint8_t>(AuthenticationResponseStatus::Rejected);
        }

        void clear(std::string& value) noexcept
        {
            if (!value.empty())
                sodium_memzero(value.data(), value.size());
            value.clear();
        }
    }

    bool encodeAuthenticationRequest(AuthenticationOperation operation,
        std::string_view accountName, const PasswordBuffer& password,
        const PasswordBuffer* serverAccessPassword, std::vector<std::byte>& encoded,
        protocol::CodecError& error)
    {
        error = protocol::CodecError::None;
        if (!validOperation(static_cast<std::uint8_t>(operation)))
        {
            error = protocol::CodecError::InvalidValue;
            return false;
        }
        protocol::PacketWriter writer;
        if (!writer.writeU32(authenticationMessageSchemaVersion)
            || !writer.writeU8(static_cast<std::uint8_t>(operation))
            || !writer.writeString(accountName, protocol::limits::accountNameBytes)
            || !writer.writeString(view(password), protocol::limits::passwordBytes)
            || !writer.writeString(serverAccessPassword == nullptr
                    ? std::string_view{} : view(*serverAccessPassword),
                protocol::limits::passwordBytes))
        {
            error = writer.error();
            return false;
        }
        encoded = writer.take();
        return true;
    }

    protocol::DecodeResult decodeAuthenticationRequest(
        std::span<const std::byte> encoded, AuthenticationRequest& request)
    {
        protocol::PacketReader reader(encoded);
        std::uint32_t schemaVersion = 0;
        std::uint8_t operation = 0;
        std::string accountName;
        std::string passwordText;
        std::string accessText;
        if (!reader.readU32(schemaVersion)
            || schemaVersion != authenticationMessageSchemaVersion
            || !reader.readU8(operation) || !validOperation(operation)
            || !reader.readString(accountName, protocol::limits::accountNameBytes)
            || !reader.readString(passwordText, protocol::limits::passwordBytes)
            || !reader.readString(accessText, protocol::limits::passwordBytes)
            || !reader.finish())
        {
            clear(passwordText);
            clear(accessText);
            return schemaVersion != authenticationMessageSchemaVersion
                    || !validOperation(operation)
                ? protocol::DecodeResult{ protocol::CodecError::InvalidValue, reader.position() }
                : reader.result();
        }

        std::string passwordError;
        auto password = PasswordBuffer::copyFrom(passwordText, passwordError);
        std::optional<PasswordBuffer> accessPassword;
        if (!accessText.empty())
            accessPassword = PasswordBuffer::copyFrom(accessText, passwordError);
        clear(passwordText);
        clear(accessText);
        if (!password || (!passwordError.empty() && !accessPassword))
            return { protocol::CodecError::InvalidValue, reader.position() };

        AuthenticationRequest decoded;
        decoded.operation = static_cast<AuthenticationOperation>(operation);
        decoded.accountName = std::move(accountName);
        decoded.password = std::move(password);
        decoded.serverAccessPassword = std::move(accessPassword);
        request = std::move(decoded);
        return reader.result();
    }

    bool encodeAuthenticationResponse(const AuthenticationResponse& response,
        std::vector<std::byte>& encoded, protocol::CodecError& error)
    {
        error = protocol::CodecError::None;
        if (!validResponseStatus(static_cast<std::uint8_t>(response.status)))
        {
            error = protocol::CodecError::InvalidValue;
            return false;
        }
        protocol::PacketWriter writer;
        if (!writer.writeU32(authenticationMessageSchemaVersion)
            || !writer.writeU8(static_cast<std::uint8_t>(response.status))
            || !writer.writeString(response.message, sMaximumResponseBytes))
        {
            error = writer.error();
            return false;
        }
        encoded = writer.take();
        return true;
    }

    protocol::DecodeResult decodeAuthenticationResponse(
        std::span<const std::byte> encoded, AuthenticationResponse& response)
    {
        protocol::PacketReader reader(encoded);
        std::uint32_t schemaVersion = 0;
        std::uint8_t status = 0;
        AuthenticationResponse decoded;
        if (!reader.readU32(schemaVersion)
            || schemaVersion != authenticationMessageSchemaVersion
            || !reader.readU8(status) || !validResponseStatus(status)
            || !reader.readString(decoded.message, sMaximumResponseBytes)
            || !reader.finish())
        {
            return schemaVersion != authenticationMessageSchemaVersion
                    || !validResponseStatus(status)
                ? protocol::DecodeResult{ protocol::CodecError::InvalidValue, reader.position() }
                : reader.result();
        }
        decoded.status = static_cast<AuthenticationResponseStatus>(status);
        response = std::move(decoded);
        return reader.result();
    }
}
