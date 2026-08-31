#ifndef OPENMW_PACKETCONSOLECOMMAND_HPP
#define OPENMW_PACKETCONSOLECOMMAND_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    class PacketConsoleCommand : public ObjectPacket
    {
    public:
        PacketConsoleCommand();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETCONSOLECOMMAND_HPP
