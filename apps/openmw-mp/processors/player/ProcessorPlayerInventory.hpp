#ifndef OPENMW_PROCESSORPLAYERINVENTORY_HPP
#define OPENMW_PROCESSORPLAYERINVENTORY_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerInventory : public PlayerProcessor
    {
    public:
        ProcessorPlayerInventory()
        {
            BPP_INIT(ID_PLAYER_INVENTORY)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerInventory(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            const bool allowed = Script::CallBoolean<
                Script::CallbackIdentity("OnPlayerInventoryIntent")>(player.getId());
            if (!allowed)
                return;

            if (!Networking::getPtr()->commitPlayerInventory(player))
            {
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerInventoryIntentRejected")>(player.getId());
                return;
            }

            Script::Call<Script::CallbackIdentity("OnPlayerInventory")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERINVENTORY_HPP
