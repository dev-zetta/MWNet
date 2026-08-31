#ifndef OPENMW_PROCESSORPLAYERLEVEL_HPP
#define OPENMW_PROCESSORPLAYERLEVEL_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerLevel : public PlayerProcessor
    {
    public:
        ProcessorPlayerLevel()
        {
            BPP_INIT(ID_PLAYER_LEVEL)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            if (player.creatureStats.mDead)
                return false;
            return Networking::getPtr()->validatePlayerLevel(player, incoming);
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
                        "OnPlayerLevelIntent")>(player.getId());
                }
                catch (...)
                {
                    networking->cancelPlayerLevelIntent(player);
                    throw;
                }
                if (!allowed)
                {
                    networking->cancelPlayerLevelIntent(player);
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerLevelIntentRejected")>(player.getId());
                    return;
                }
                if (!networking->commitPlayerLevel(player))
                {
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerLevelIntentRejected")>(player.getId());
                    return;
                }
                Script::Call<Script::CallbackIdentity("OnPlayerLevel")>(player.getId());
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERLEVEL_HPP
