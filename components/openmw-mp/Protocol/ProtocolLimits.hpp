#ifndef OPENMW_MP_PROTOCOL_LIMITS_HPP
#define OPENMW_MP_PROTOCOL_LIMITS_HPP

#include <cstddef>
#include <cstdint>

namespace mwmp::protocol::limits
{
    inline constexpr std::size_t normalMessageBytes = 1U * 1024U * 1024U;
    inline constexpr std::size_t bulkChunkBytes = 256U * 1024U;
    inline constexpr std::size_t bulkTransferBytes = 64U * 1024U * 1024U;

    inline constexpr std::size_t accountNameBytes = 64;
    inline constexpr std::size_t playerNameBytes = 64;
    inline constexpr std::size_t passwordBytes = 128;
    inline constexpr std::size_t chatMessageBytes = 512;
    inline constexpr std::size_t commandBytes = 4U * 1024U;
    inline constexpr std::size_t defaultStringBytes = 4U * 1024U;

    inline constexpr std::uint32_t defaultCollectionElements = 4096;
    inline constexpr std::uint32_t actorChanges = 3000;
    inline constexpr std::uint32_t objectChanges = 3000;
    inline constexpr std::uint32_t spellEffects = 256;
    inline constexpr std::size_t mapTileImageBytes = 1800;

    inline constexpr double messagesPerSecond = 512.0;
    inline constexpr double messageBurst = messagesPerSecond * 2.0;
    inline constexpr double bytesPerSecond = 2.0 * 1024.0 * 1024.0;
    inline constexpr double byteBurst = bytesPerSecond * 2.0;
    inline constexpr std::size_t chatMessagesPerWindow = 5;
}

#endif
