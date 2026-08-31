#ifndef OPENMW_PROCESSORPLAYERSPELLBOOK_HPP
#define OPENMW_PROCESSORPLAYERSPELLBOOK_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerSpellbook : public PlayerProcessor
    {
    public:
        ProcessorPlayerSpellbook()
        {
            BPP_INIT(ID_PLAYER_SPELLBOOK)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerSpellbook(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (!Networking::getPtr()->commitPlayerSpellbook(player))
                return;
            Script::Call<Script::CallbackIdentity("OnPlayerSpellbook")>(player.getId());
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERSPELLBOOK_HPP
