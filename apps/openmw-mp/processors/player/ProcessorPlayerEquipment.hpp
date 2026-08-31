#ifndef OPENMW_PROCESSORPLAYEREQUIPMENT_HPP
#define OPENMW_PROCESSORPLAYEREQUIPMENT_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerEquipment : public PlayerProcessor
    {
    public:
        ProcessorPlayerEquipment()
        {
            BPP_INIT(ID_PLAYER_EQUIPMENT)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerEquipment(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            const bool allowed = Script::CallBoolean<
                Script::CallbackIdentity("OnPlayerEquipmentIntent")>(player.getId());
            if (!allowed)
                return;

            if (!Networking::getPtr()->commitPlayerEquipment(player))
            {
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerEquipmentIntentRejected")>(player.getId());
                return;
            }

            player.sendToLoaded(&packet);
            Script::Call<Script::CallbackIdentity("OnPlayerEquipment")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYEREQUIPMENT_HPP
