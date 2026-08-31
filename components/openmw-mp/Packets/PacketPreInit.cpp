#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketPreInit.hpp"

mwmp::PacketPreInit::PacketPreInit()
    : BasePacket()
    , checksums(nullptr)
{
    packetID = ID_GAME_PREINIT;
}

void mwmp::PacketPreInit::Packet(bool send)
{
    BasePacket::Packet(send);
    if (!packetValid || checksums == nullptr)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return;
    }

    uint32_t numberOfChecksums = static_cast<std::uint32_t>(checksums->size());
    if (!RWCount(numberOfChecksums, send, maxPlugins))
        return;

    struct NAS
    {
        uint32_t hashN = 0;
        uint32_t strSize = 0;
    };

    std::vector<NAS> NumberOfHashesAndStrSizes(numberOfChecksums);

    PluginContainer::const_iterator checksumIt = checksums->begin();

    for (auto &&nas : NumberOfHashesAndStrSizes)
    {
        if (send)
        {
            nas.strSize = checksumIt->first.size();
            nas.hashN = checksumIt++->second.size();
        }
        if (!RW(nas.hashN, send) || !RW(nas.strSize, send))
            return;

        if (nas.strSize > pluginNameMaxLength)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong string length %d when maximum length is %d",
                        nas.strSize,
                        pluginNameMaxLength);
        else if (nas.hashN > maxHashes)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong  number of hashes %d when maximum is %d", nas.hashN, maxHashes);
        else
            continue;
        invalidate(protocol::CodecError::LimitExceeded);
        return;
    }

    PluginContainer decodedChecksums;
    PluginContainer& target = send ? *checksums : decodedChecksums;
    if (!send)
        decodedChecksums.resize(numberOfChecksums);

    auto numberOfHashesIt = NumberOfHashesAndStrSizes.cbegin();

    for (auto &&checksum : target)
    {
        if (!RW(checksum.first, send, false, numberOfHashesIt->strSize)
            || checksum.first.size() != numberOfHashesIt->strSize)
        {
            invalidate(protocol::CodecError::InvalidValue);
            return;
        }

        checksum.second.resize(numberOfHashesIt->hashN);
        for (auto &&hash : checksum.second)
        {
            if (!RW(hash, send))
                return;
        }
        ++numberOfHashesIt;
    }

    if (!send)
    {
        if (unreadPayloadBytes() != 0)
        {
            invalidate(protocol::CodecError::TrailingData);
            return;
        }
        *checksums = std::move(decodedChecksums);
    }
}

void mwmp::PacketPreInit::setChecksums(mwmp::PacketPreInit::PluginContainer *newChecksums)
{
    checksums = newChecksums;
}
