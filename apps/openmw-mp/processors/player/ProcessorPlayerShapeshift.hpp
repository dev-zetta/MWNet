#ifndef OPENMW_PROCESSORPLAYERSHAPESHIFT_HPP
#define OPENMW_PROCESSORPLAYERSHAPESHIFT_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerShapeshift : public PlayerProcessor
    {
    public:
        ProcessorPlayerShapeshift()
        {
            BPP_INIT(ID_PLAYER_SHAPESHIFT)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerShapeshift(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            Networking* networking = Networking::getPtr();
            bool allowed = false;
            try
            {
                allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnPlayerShapeshiftIntent")>(player.getId());
            }
            catch (...)
            {
                networking->cancelPlayerShapeshiftIntent(player);
                throw;
            }
            if (!allowed)
            {
                networking->cancelPlayerShapeshiftIntent(player);
                const char* reason = "denied by script";
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerShapeshiftIntentRejected")>(player.getId(),
                    reason);
                return;
            }
            if (!networking->commitPlayerShapeshift(player))
            {
                const char* reason = "canonical shapeshift validation failed";
                Script::Call<Script::CallbackIdentity(
                    "OnPlayerShapeshiftIntentRejected")>(player.getId(),
                    reason);
                return;
            }

            packet.Send(true);

            Script::Call<Script::CallbackIdentity("OnPlayerShapeshift")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERSHAPESHIFT_HPP
