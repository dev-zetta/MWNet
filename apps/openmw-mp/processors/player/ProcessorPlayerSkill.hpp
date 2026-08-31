#ifndef OPENMW_PROCESSORPLAYERSKILL_HPP
#define OPENMW_PROCESSORPLAYERSKILL_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerSkill : public PlayerProcessor
    {
    public:
        ProcessorPlayerSkill()
        {
            BPP_INIT(ID_PLAYER_SKILL)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            if (player.creatureStats.mDead)
                return false;
            return Networking::getPtr()->validatePlayerSkills(player, incoming);
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
                        "OnPlayerSkillIntent")>(player.getId());
                }
                catch (...)
                {
                    networking->cancelPlayerSkillIntent(player);
                    throw;
                }
                if (!allowed)
                {
                    networking->cancelPlayerSkillIntent(player);
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerSkillIntentRejected")>(player.getId());
                    return;
                }
                if (!networking->commitPlayerSkills(player))
                {
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerSkillIntentRejected")>(player.getId());
                    return;
                }
                player.sendToLoaded(&packet);

                Script::Call<Script::CallbackIdentity("OnPlayerSkill")>(player.getId());
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERSKILL_HPP
