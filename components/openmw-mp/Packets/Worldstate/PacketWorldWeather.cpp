#include "PacketWorldWeather.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketWorldWeather::PacketWorldWeather() : WorldstatePacket()
{
    packetID = ID_WORLD_WEATHER;
}

void PacketWorldWeather::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    Field(worldstate->forceWeather);
    Field(worldstate->weather.region, true);
    Field(worldstate->weather.currentWeather);
    Field(worldstate->weather.nextWeather);
    Field(worldstate->weather.queuedWeather);
    Field(worldstate->weather.transitionFactor);
}
