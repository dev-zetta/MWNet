#ifndef OPENMW_PROCESSORPLAYERATTRIBUTE_HPP
#define OPENMW_PROCESSORPLAYERATTRIBUTE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerAttribute : public PlayerProcessor
    {
    public:
        ProcessorPlayerAttribute()
        {
            BPP_INIT(ID_PLAYER_ATTRIBUTE)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            if (player.creatureStats.mDead)
                return false;
            return Networking::getPtr()->validatePlayerAttributes(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            if (!player.creatureStats.mDead)
            {
                Networking* networking = Networking::getPtr();
                bool allowed = false;
                try
                {
                    allowed = Script::CallBoolean<Script::CallbackIdentity(
                        "OnPlayerAttributeIntent")>(player.getId());
                }
                catch (...)
                {
                    networking->cancelPlayerAttributeIntent(player);
                    throw;
                }
                if (!allowed)
                {
                    networking->cancelPlayerAttributeIntent(player);
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerAttributeIntentRejected")>(player.getId());
                    return;
                }
                if (!networking->commitPlayerAttributes(player))
                {
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerAttributeIntentRejected")>(player.getId());
                    return;
                }

                player.sendToLoaded(&packet);

                Script::Call<Script::CallbackIdentity("OnPlayerAttribute")>(player.getId());
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERATTRIBUTE_HPP
