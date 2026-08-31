#ifndef OPENMW_PACKETCLIENTSCRIPTSETTINGS_HPP
#define OPENMW_PACKETCLIENTSCRIPTSETTINGS_HPP

#include <components/openmw-mp/Packets/Worldstate/WorldstatePacket.hpp>

namespace mwmp
{
    class PacketClientScriptSettings : public WorldstatePacket
    {
    public:
        PacketClientScriptSettings();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETCLIENTSCRIPTSETTINGS_HPP
