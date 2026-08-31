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

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                "OnPlayerSpellsActiveIntent")>(player.getId());
            if (!allowed)
                return;

            if (!Networking::getPtr()->commitPlayerActiveEffects(player))
            {
                const char* reason = "canonical active-effect validation failed";
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerSpellsActiveIntentRejected")>(player.getId(), reason);
                return;
            }

            Networking* networking = Networking::getPtr();
            try
            {
                Script::Call<Script::CallbackIdentity("OnPlayerSpellsActive")>(player.getId());
            }
            catch (...)
            {
                networking->finishPlayerActiveEffectIntent(player);
                throw;
            }
            if (!networking->finishPlayerActiveEffectIntent(player))
                player.sendToLoaded(&packet);
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP
