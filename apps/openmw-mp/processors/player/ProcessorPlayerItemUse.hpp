#ifndef OPENMW_PROCESSORPLAYERITEMUSE_HPP
#define OPENMW_PROCESSORPLAYERITEMUSE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerItemUse : public PlayerProcessor
    {
    public:
        ProcessorPlayerItemUse()
        {
            BPP_INIT(ID_PLAYER_ITEM_USE)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerItemUse(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                "OnPlayerItemUseIntent")>(player.getId());
            if (!allowed)
            {
                const char* reason = "denied by script";
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerItemUseIntentRejected")>(player.getId(), reason);
                return;
            }
            std::string rejectionReason;
            if (!Networking::getPtr()->resolvePlayerItemUse(
                    player, rejectionReason))
            {
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerItemUseIntentRejected")>(
                        player.getId(), rejectionReason.c_str());
                return;
            }
            Script::Call<Script::CallbackIdentity("OnPlayerItemUse")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERITEMUSE_HPP
