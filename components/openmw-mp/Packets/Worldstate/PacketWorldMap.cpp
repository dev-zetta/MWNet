#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketWorldMap.hpp"

using namespace mwmp;

PacketWorldMap::PacketWorldMap() : WorldstatePacket()
{
    packetID = ID_WORLD_MAP;
}

void PacketWorldMap::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    uint32_t changesCount = 0;

    if (send)
        changesCount = static_cast<uint32_t>(worldstate->mapTiles.size());

    if (!CollectionSize(changesCount))
        return;

    if (!send)
    {
        worldstate->mapTiles.clear();
        worldstate->mapTiles.resize(changesCount);
    }

    for (auto &&mapTile : worldstate->mapTiles)
    {
        Field(mapTile.x);
        Field(mapTile.y);

        uint32_t imageDataSize = 0;

        if (send)
            imageDataSize = static_cast<uint32_t>(mapTile.imageData.size());

        if (!CollectionSize(imageDataSize, protocol::limits::mapTileImageBytes))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Processed invalid ID_WORLD_MAP packet where tile %i, %i had an imageDataSize of %i",
                mapTile.x, mapTile.y, imageDataSize);
            LOG_APPEND(TimedLog::LOG_ERROR, "- The packet was ignored after that point");
            return;
        }

        if (!send)
        {
            mapTile.imageData.clear();
            mapTile.imageData.resize(imageDataSize);
        }

        for (auto &&imageChar : mapTile.imageData)
        {
            Field(imageChar);
        }
    }
}
