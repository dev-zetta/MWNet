#ifndef TES3MP_DISCOVERY_PROTOCOL_HPP
#define TES3MP_DISCOVERY_PROTOCOL_HPP

#include <components/openmw-mp/Security/ServerIdentity.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mwmp::discovery
{
    using Json = boost::property_tree::ptree;
    inline constexpr std::size_t maximumPayload = 256 * 1024;
    inline constexpr std::size_t maximumBody = 2 * 1024 * 1024;
    inline constexpr std::size_t maximumListings = 1000;
    inline constexpr std::string_view signatureDomain = "TES3MP discovery v1\n";
    struct Content
    {
        std::string name;
        std::vector<std::uint32_t> hashes;
        bool operator==(const Content&) const = default;
    };
    struct Listing
    {
        std::string host, name, version;
        std::uint16_t port = 25565, protocol = 12, players = 0, capacity = 64;
        bool password = false;
        std::vector<Content> content;
        bool operator==(const Listing&) const = default;
    };
    struct Envelope
    {
        std::string origin, operation, nonce, publicKey, payload, signature;
    };
    std::string hex(std::span<const unsigned char> bytes);
    std::vector<unsigned char> unhex(std::string_view text, std::size_t maximum);
    std::string randomNonce();
    std::string fingerprint(const Envelope& envelope);
    std::vector<unsigned char> encode(const Listing& listing);
    Listing decode(std::span<const unsigned char> bytes);
    std::vector<unsigned char> signingBytes(const Envelope& envelope);
    Envelope sign(const security::ServerIdentity& identity, const Listing& listing,
        std::string origin, std::string operation, std::string nonce);
    Listing verify(const Envelope& envelope, std::string_view origin);
    Json toJson(const Envelope& envelope);
    Envelope envelopeFromJson(const Json& json);
    Json parseJson(std::string_view text);
    std::string writeJson(const Json& json);
    void requireFields(const Json& json, std::initializer_list<std::string_view> fields);
    std::string scalar(const Json& json, const char* key, std::size_t maximum);
    bool validOrigin(std::string_view origin);
    bool publicAddress(std::string_view address);
}
#endif
