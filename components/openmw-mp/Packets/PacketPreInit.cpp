#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketPreInit.hpp"

mwmp::PacketPreInit::PacketPreInit(RakNet::RakPeerInterface *peer)
    : BasePacket(peer)
    , checksums(nullptr)
{
    packetID = ID_GAME_PREINIT;
}

void mwmp::PacketPreInit::Packet(RakNet::BitStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);
    if (!packetValid || checksums == nullptr)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return;
    }

    const std::uint64_t packetSize = (bs->GetNumberOfUnreadBits() + 7U) / 8U;
    std::uint64_t expectedPacketSize = sizeof(std::uint32_t);
    if (!send && expectedPacketSize > packetSize)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong packet size %d when expected %d", packetSize, expectedPacketSize);
        packetValid = false;
        return;
    }

    uint32_t numberOfChecksums = static_cast<std::uint32_t>(checksums->size());
    if (!RW(numberOfChecksums, send))
        return;

    if (numberOfChecksums > maxPlugins)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong number of checksums %d when maximum is %d", numberOfChecksums, maxPlugins);
        packetValid = false;
        return;
    }

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

        expectedPacketSize += sizeof(nas.hashN) + sizeof(nas.strSize) + sizeof(std::uint16_t) + nas.strSize
            + static_cast<std::uint64_t>(nas.hashN) * sizeof(HashList::value_type);

        if (nas.strSize > pluginNameMaxLength)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong string length %d when maximum length is %d",
                        nas.strSize,
                        pluginNameMaxLength);
        else if (nas.hashN > maxHashes)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong  number of hashes %d when maximum is %d", nas.hashN, maxHashes);
        else
            continue;
        packetValid = false;
        return;
    }

    if (!send && expectedPacketSize == packetSize) // server accepted plugin list via sending "empty" packet
        return;

    if (!send && expectedPacketSize > packetSize)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong packet size %d when expected %d", packetSize, expectedPacketSize);
        packetValid = false;
        return;
    }

    checksums->resize(numberOfChecksums);

    auto numberOfHashesIt = NumberOfHashesAndStrSizes.cbegin();

    for (auto &&checksum : *checksums)
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

    if (!send && bs->GetNumberOfUnreadBits() != 0)
        invalidate(protocol::CodecError::TrailingData);
}

void mwmp::PacketPreInit::setChecksums(mwmp::PacketPreInit::PluginContainer *newChecksums)
{
    checksums = newChecksums;
}
