#ifndef OPENMW_PACKETGUIBOXES_HPP
#define OPENMW_PACKETGUIBOXES_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketGUIBoxes : public PlayerPacket
    {
    public:
        PacketGUIBoxes();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETGUIBOXES_HPP
