#include "PacketWorldDestinationOverride.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

PacketWorldDestinationOverride::PacketWorldDestinationOverride() : WorldstatePacket()
{
    packetID = ID_WORLD_DESTINATION_OVERRIDE;
}

void PacketWorldDestinationOverride::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    uint32_t destinationCount = 0;

    if (send)
        destinationCount = static_cast<uint32_t>(worldstate->destinationOverrides.size());

    if (!CollectionSize(destinationCount))
        return;

    if (!send)
    {
        worldstate->destinationOverrides.clear();
    }

    std::string mapIndex;
    std::string mapValue;

    if (send)
    {
        for (auto &&destinationOverride : worldstate->destinationOverrides)
        {
            mapIndex = destinationOverride.first;
            mapValue = destinationOverride.second;
            Field(mapIndex, false);
            Field(mapValue, false);
        }
    }
    else
    {
        for (unsigned int n = 0; n < destinationCount; n++)
        {
            Field(mapIndex, false);
            Field(mapValue, false);
            worldstate->destinationOverrides[mapIndex] = mapValue;
        }
    }
}
