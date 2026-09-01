#include <components/openmw-mp/Security/AuthenticationMessages.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::string printable(const std::uint8_t* data, std::size_t size,
        std::size_t offset, std::size_t maximumSize)
    {
        std::string value;
        if (offset >= size)
            return value;
        const auto count = std::min(size - offset, maximumSize);
        value.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
            value.push_back(static_cast<char>(' ' + data[offset + index] % 95));
        return value;
    }

    void mutate(std::vector<std::byte>& encoded, const std::uint8_t* data,
        std::size_t size)
    {
        if (size < 3 || encoded.empty())
            return;
        const auto offset = static_cast<std::size_t>(data[1]) % encoded.size();
        encoded[offset] ^= static_cast<std::byte>(data[2]);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    mwmp::security::AuthenticationRequest request;
    mwmp::security::AuthenticationResponse response;
    (void)mwmp::security::decodeAuthenticationRequest(bytes, request);
    (void)mwmp::security::decodeAuthenticationResponse(bytes, response);

    const auto accountName = printable(data, size, 1, 64);
    auto passwordText = printable(data, size, 2, 128);
    if (passwordText.empty())
        passwordText = "p";
    auto accessText = printable(data, size, 3, 128);
    std::string passwordError;
    auto password = mwmp::security::PasswordBuffer::copyFrom(passwordText, passwordError);
    auto accessPassword = mwmp::security::PasswordBuffer::copyFrom(accessText, passwordError);
    if (password && accessPassword)
    {
        std::vector<std::byte> encoded;
        mwmp::protocol::CodecError error = mwmp::protocol::CodecError::None;
        const auto operation = size > 0 && (data[0] & 1) != 0
            ? mwmp::security::AuthenticationOperation::Register
            : mwmp::security::AuthenticationOperation::Login;
        const auto* access = size > 0 && (data[0] & 2) != 0
            ? &*accessPassword : nullptr;
        if (mwmp::security::encodeAuthenticationRequest(
                operation, accountName, *password, access, encoded, error))
        {
            mwmp::security::AuthenticationRequest validRequest;
            (void)mwmp::security::decodeAuthenticationRequest(encoded, validRequest);
            mutate(encoded, data, size);
            (void)mwmp::security::decodeAuthenticationRequest(encoded, request);
        }

        mwmp::security::AuthenticationResponse generated;
        generated.status = static_cast<mwmp::security::AuthenticationResponseStatus>(
            1 + (size > 0 ? data[0] % 6 : 0));
        generated.message = printable(data, size, 1, 512);
        if (mwmp::security::encodeAuthenticationResponse(generated, encoded, error))
        {
            mwmp::security::AuthenticationResponse validResponse;
            (void)mwmp::security::decodeAuthenticationResponse(encoded, validResponse);
            mutate(encoded, data, size);
            (void)mwmp::security::decodeAuthenticationResponse(encoded, response);
        }
    }
    return 0;
}
