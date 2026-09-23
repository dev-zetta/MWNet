#include "Protocol.hpp"
#include <components/openmw-mp/Security/SecureSession.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mwmp::discovery
{
    namespace
    {
        void require(bool condition, const char* message)
        {
            if (!condition) throw std::invalid_argument(message);
        }
        bool validUtf8(std::string_view s)
        {
            for (std::size_t i = 0; i < s.size();)
            {
                const auto first = static_cast<unsigned char>(s[i++]);
                if (first < 128) continue;
                unsigned count = first >= 0xc2 && first <= 0xdf ? 1
                    : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
                if (count == 0 || i + count > s.size()) return false;
                std::uint32_t value = first & ((1U << (6-count))-1);
                for (unsigned n = 0; n < count; ++n)
                {
                    const auto next = static_cast<unsigned char>(s[i++]);
                    if ((next & 0xc0) != 0x80) return false;
                    value = (value << 6) | (next & 0x3f);
                }
                if ((count == 1 && value < 0x80) || (count == 2 && value < 0x800)
                    || (count == 3 && value < 0x10000) || value > 0x10ffff
                    || (value >= 0xd800 && value <= 0xdfff)) return false;
            }
            return true;
        }
        struct Writer
        {
            std::vector<unsigned char> bytes;
            void number(std::uint32_t n)
            {
                for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(n >> (8 * i)));
            }
            void text(std::string_view s)
            {
                number(static_cast<std::uint32_t>(s.size()));
                bytes.insert(bytes.end(), s.begin(), s.end());
            }
        };
        struct Reader
        {
            std::span<const unsigned char> bytes;
            std::uint32_t number()
            {
                require(bytes.size() >= 4, "truncated discovery payload");
                std::uint32_t n = 0;
                for (unsigned i = 0; i < 4; ++i) n |= std::uint32_t(bytes[i]) << (8 * i);
                bytes = bytes.subspan(4);
                return n;
            }
            std::uint16_t shortNumber()
            {
                auto n = number();
                require(n <= 65535, "discovery integer out of range");
                return static_cast<std::uint16_t>(n);
            }
            std::string text(std::size_t maximum)
            {
                auto n = number();
                require(n <= maximum && n <= bytes.size(), "invalid discovery string length");
                std::string s(bytes.begin(), bytes.begin() + n);
                bytes = bytes.subspan(n);
                require(std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }),
                    "control character in discovery string");
                require(validUtf8(s), "invalid discovery UTF-8");
                return s;
            }
        };
        void validate(const Listing& l)
        {
            require(!l.name.empty() && l.name.size() <= 128 && l.version.size() <= 64,
                "invalid server name or version");
            require(l.capacity > 0 && l.players <= l.capacity && l.protocol > 0,
                "invalid server capacity or protocol");
            require(l.host.size() <= 253 && l.host.find('%') == std::string::npos
                && security::TrustStore::canonicalEndpoint(l.host, l.port).has_value(), "invalid server endpoint");
            require(l.content.size() <= 1000, "too many content files");
            std::set<std::string> names;
            for (const auto& c : l.content)
                require(!c.name.empty() && c.name.size() <= 256 && c.hashes.size() <= 50
                    && names.insert(c.name).second, "invalid content requirements");
        }
    }
    std::string hex(std::span<const unsigned char> bytes)
    {
        std::string s(bytes.size() * 2 + 1, '\0');
        sodium_bin2hex(s.data(), s.size(), bytes.data(), bytes.size());
        s.pop_back();
        return s;
    }
    std::vector<unsigned char> unhex(std::string_view s, std::size_t maximum)
    {
        require(s.size() % 2 == 0 && s.size() / 2 <= maximum, "invalid hex length");
        require(std::all_of(s.begin(), s.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }), "noncanonical hex");
        std::vector<unsigned char> out(s.size() / 2);
        require(sodium_hex2bin(out.data(), out.size(), s.data(), s.size(), nullptr, nullptr, nullptr) == 0,
            "invalid hex");
        return out;
    }
    std::string randomNonce()
    {
        std::array<unsigned char, 32> bytes{};
        randombytes_buf(bytes.data(), bytes.size());
        return hex(bytes);
    }
    std::string fingerprint(const Envelope& e)
    {
        auto key = unhex(e.publicKey, crypto_sign_PUBLICKEYBYTES);
        require(key.size() == crypto_sign_PUBLICKEYBYTES, "invalid public key");
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> array{};
        std::copy(key.begin(), key.end(), array.begin());
        return security::fingerprint(array);
    }
    std::vector<unsigned char> encode(const Listing& l)
    {
        validate(l);
        Writer w;
        w.number(1);
        w.text(l.host); w.number(l.port); w.text(l.name); w.text(l.version);
        w.number(l.protocol); w.number(l.players); w.number(l.capacity); w.number(l.password ? 1 : 0);
        w.number(static_cast<std::uint32_t>(l.content.size()));
        for (const auto& c : l.content)
        {
            w.text(c.name); w.number(static_cast<std::uint32_t>(c.hashes.size()));
            for (auto h : c.hashes) w.number(h);
        }
        require(w.bytes.size() <= maximumPayload, "discovery payload too large");
        // Apply the same text validation to locally produced data.
        (void)decode(w.bytes);
        return w.bytes;
    }
    Listing decode(std::span<const unsigned char> bytes)
    {
        require(bytes.size() <= maximumPayload, "discovery payload too large");
        Reader r{ bytes };
        require(r.number() == 1, "unsupported discovery payload version");
        Listing l;
        l.host = r.text(253); l.port = r.shortNumber(); l.name = r.text(128); l.version = r.text(64);
        l.protocol = r.shortNumber(); l.players = r.shortNumber(); l.capacity = r.shortNumber();
        auto password = r.number();
        require(password <= 1, "invalid password flag");
        l.password = password != 0;
        auto count = r.number();
        require(count <= 1000, "too many content files");
        for (std::uint32_t i = 0; i < count; ++i)
        {
            Content c;
            c.name = r.text(256);
            auto hashes = r.number();
            require(hashes <= 50, "too many content hashes");
            for (std::uint32_t j = 0; j < hashes; ++j) c.hashes.push_back(r.number());
            l.content.push_back(std::move(c));
        }
        require(r.bytes.empty(), "trailing discovery payload bytes");
        validate(l);
        return l;
    }
    bool validOrigin(std::string_view s)
    {
        if (!s.starts_with("https://") || s.size() > 300 || s.size() <= 8) return false;
        auto authority = s.substr(8);
        return authority.find_first_of("/\\?#@ \t\r\n") == std::string_view::npos
            && std::all_of(authority.begin(), authority.end(), [](unsigned char c) { return c >= 33 && c < 127; });
    }
    std::vector<unsigned char> signingBytes(const Envelope& e)
    {
        require(validOrigin(e.origin), "directory must be an HTTPS origin without a path");
        require(e.operation == "announce" || e.operation == "withdraw", "invalid discovery operation");
        require(unhex(e.nonce, 32).size() == 32, "invalid challenge");
        auto payload = unhex(e.payload, maximumPayload);
        Writer w;
        w.text(e.origin); w.text(e.operation); w.text(e.nonce); w.text(e.publicKey);
        w.number(static_cast<std::uint32_t>(payload.size()));
        w.bytes.insert(w.bytes.end(), payload.begin(), payload.end());
        return w.bytes;
    }
    Envelope sign(const security::ServerIdentity& identity, const Listing& listing,
        std::string origin, std::string operation, std::string nonce)
    {
        Envelope e{ std::move(origin), std::move(operation), std::move(nonce), hex(identity.publicKey()),
            hex(encode(listing)), {} };
        e.signature = hex(identity.signDiscovery(signingBytes(e)));
        return e;
    }
    Listing verify(const Envelope& e, std::string_view origin)
    {
        require(e.origin == origin, "wrong directory origin");
        const auto body = signingBytes(e);
        std::vector<unsigned char> bytes(signatureDomain.begin(), signatureDomain.end());
        bytes.insert(bytes.end(), body.begin(), body.end());
        auto key = unhex(e.publicKey, 32), sig = unhex(e.signature, 64);
        require(key.size() == 32 && sig.size() == 64
            && crypto_sign_verify_detached(sig.data(), bytes.data(), bytes.size(), key.data()) == 0,
            "invalid discovery signature");
        return decode(unhex(e.payload, maximumPayload));
    }
    void requireFields(const Json& j, std::initializer_list<std::string_view> fields)
    {
        require(j.data().empty() && j.size() == fields.size(), "invalid discovery object");
        std::set<std::string> seen;
        for (const auto& [key, value] : j)
        {
            (void)value;
            require(std::find(fields.begin(), fields.end(), key) != fields.end()
                && seen.insert(key).second, "unknown or duplicate discovery field");
        }
    }
    std::string scalar(const Json& j, const char* key, std::size_t maximum)
    {
        const auto& value = j.get_child(key);
        require(value.empty() && value.data().size() <= maximum, "invalid discovery scalar");
        return value.data();
    }
    Json toJson(const Envelope& e)
    {
        Json j;
        j.put("origin", e.origin); j.put("operation", e.operation); j.put("nonce", e.nonce);
        j.put("publicKey", e.publicKey); j.put("payload", e.payload); j.put("signature", e.signature);
        return j;
    }
    Envelope envelopeFromJson(const Json& j)
    {
        requireFields(j, {"origin", "operation", "nonce", "publicKey", "payload", "signature"});
        return { scalar(j,"origin",300), scalar(j,"operation",8), scalar(j,"nonce",64),
            scalar(j,"publicKey",64), scalar(j,"payload",2*maximumPayload), scalar(j,"signature",128) };
    }
    Json parseJson(std::string_view text)
    {
        require(text.size() <= maximumBody, "discovery body too large");
        // Bound nesting before the recursive property-tree parser sees untrusted input.
        bool quoted = false, escape = false;
        int depth = 0;
        for (char c : text)
        {
            if (quoted) { if (escape) escape = false; else if (c == '\\') escape = true;
                else if (c == '"') quoted = false; }
            else if (c == '"') quoted = true;
            else if (c == '{' || c == '[') require(++depth <= 8, "discovery JSON too deeply nested");
            else if (c == '}' || c == ']') --depth;
        }
        std::istringstream stream{ std::string(text) };
        Json j;
        boost::property_tree::read_json(stream, j);
        return j;
    }
    std::string writeJson(const Json& j)
    {
        std::ostringstream stream;
        boost::property_tree::write_json(stream, j, false);
        auto result = stream.str();
        // property_tree cannot represent the distinction between [] and an empty string.
        if (j.find("servers") != j.not_found() && j.get_child("servers").empty())
        {
            const auto position = result.find("\"servers\":\"\"");
            if (position != std::string::npos) result.replace(position, 12, "\"servers\":[]");
        }
        return result;
    }
    bool publicAddress(std::string_view text)
    {
        boost::system::error_code error;
        const auto ip = boost::asio::ip::make_address(std::string(text), error);
        if (error || ip.is_loopback() || ip.is_unspecified() || ip.is_multicast()) return false;
        if (ip.is_v4())
        {
            const auto b = ip.to_v4().to_bytes();
            return b[0] != 0 && b[0] != 10 && b[0] != 127 && b[0] < 224
                && !(b[0] == 100 && (b[1] & 192) == 64)
                && !(b[0] == 169 && b[1] == 254) && !(b[0] == 172 && b[1] >= 16 && b[1] <= 31)
                && !(b[0] == 192 && (b[1] == 0 || b[1] == 168 || (b[1] == 88 && b[2] == 99)))
                && !(b[0] == 198 && (b[1] == 18 || b[1] == 19 || (b[1] == 51 && b[2] == 100)))
                && !(b[0] == 203 && b[1] == 0 && b[2] == 113);
        }
        const auto b = ip.to_v6().to_bytes();
        // Global unicast only, excluding special-use, documentation and transition ranges.
        return (b[0] & 224) == 32 && !(b[0] == 32 && b[1] == 1 && b[2] < 2)
            && !(b[0] == 32 && b[1] == 1 && b[2] == 13 && b[3] == 184)
            && !(b[0] == 32 && b[1] == 2) && !(b[0] == 63 && b[1] == 255);
    }
}
