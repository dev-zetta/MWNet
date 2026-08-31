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

    RW(worldstate->forceWeather, send);
    RW(worldstate->weather.region, send, true);
    RW(worldstate->weather.currentWeather, send);
    RW(worldstate->weather.nextWeather, send);
    RW(worldstate->weather.queuedWeather, send);
    RW(worldstate->weather.transitionFactor, send);
}
