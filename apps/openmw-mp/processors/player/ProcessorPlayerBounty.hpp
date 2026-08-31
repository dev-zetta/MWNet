#ifndef OPENMW_PROCESSORPLAYERBOUNTY_HPP
#define OPENMW_PROCESSORPLAYERBOUNTY_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerBounty : public PlayerProcessor
    {
    public:
        ProcessorPlayerBounty()
        {
            BPP_INIT(ID_PLAYER_BOUNTY)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerBounty(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            Networking* networking = Networking::getPtr();
            bool allowed = false;
            try
            {
                allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnPlayerBountyIntent")>(player.getId());
            }
            catch (...)
            {
                networking->cancelPlayerBountyIntent(player);
                throw;
            }
            if (!allowed)
            {
                networking->cancelPlayerBountyIntent(player);
                const char* reason = "denied by script";
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerBountyIntentRejected")>(player.getId(), reason);
                return;
            }
            if (!networking->commitPlayerBounty(player))
            {
                const char* reason = "canonical bounty validation failed";
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerBountyIntentRejected")>(player.getId(), reason);
                return;
            }
            Script::Call<Script::CallbackIdentity("OnPlayerBounty")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERBOUNTY_HPP
