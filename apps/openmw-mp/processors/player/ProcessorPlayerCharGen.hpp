#ifndef OPENMW_PROCESSORPLAYERCHARGEN_HPP
#define OPENMW_PROCESSORPLAYERCHARGEN_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerCharGen : public PlayerProcessor
    {
    public:
        ProcessorPlayerCharGen()
        {
            BPP_INIT(ID_PLAYER_CHARGEN)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return player.characterCreation().accepts(
                incoming.charGenState.currentStage, incoming.charGenState.endStage);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (player.charGenState.currentStage == player.charGenState.endStage)
            {
                std::string error;
                if (!Networking::getPtr()->finalizePlayerCharacter(player, error))
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Rejected character creation: %s", error.c_str());
                    Networking::getPtr()->kickPlayer(player.guid);
                    return;
                }
                Script::Call<Script::CallbackIdentity("OnPlayerEndCharGen")>(player.getId());
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCHARGEN_HPP
