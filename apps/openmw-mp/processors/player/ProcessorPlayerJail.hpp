#ifndef OPENMW_PROCESSORPLAYERJAIL_HPP
#define OPENMW_PROCESSORPLAYERJAIL_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerJail : public PlayerProcessor
    {
    public:
        ProcessorPlayerJail()
        {
            BPP_INIT(ID_PLAYER_JAIL)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerJailCompletion(
                player, incoming);
        }

        void Do(PlayerPacket&, Player& player) override
        {
            const unsigned long long sentenceId = player.jailSentenceId;
            if (!Networking::getPtr()->completePlayerJail(player))
                return;

            Script::Call<Script::CallbackIdentity(
                "OnPlayerJailComplete")>(player.getId(), sentenceId);
        }
    };
}

#endif
