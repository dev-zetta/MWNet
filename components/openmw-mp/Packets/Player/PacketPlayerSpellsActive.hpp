#ifndef OPENMW_PACKETPLAYERSPELLSACTIVE_HPP
#define OPENMW_PACKETPLAYERSPELLSACTIVE_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerSpellsActive : public PlayerPacket
    {
    public:
        PacketPlayerSpellsActive();

        virtual void Packet(bool send);

    protected:
        static inline constexpr int maxEffects = 20;
    };
}

#endif //OPENMW_PACKETPLAYERSPELLSACTIVE_HPP
