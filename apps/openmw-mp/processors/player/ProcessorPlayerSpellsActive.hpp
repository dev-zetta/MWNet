#ifndef OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP
#define OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerSpellsActive : public PlayerProcessor
    {
    public:
        ProcessorPlayerSpellsActive()
        {
            BPP_INIT(ID_PLAYER_SPELLS_ACTIVE)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerActiveEffects(player, incoming);
        }

        void Do(PlayerPacket&, Player& player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());
            // Protocol 11 clients only acknowledge local expiry. The server
            // owns the effect clock and never relays or applies this packet.
            player.spellsActiveChanges = {};
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP
