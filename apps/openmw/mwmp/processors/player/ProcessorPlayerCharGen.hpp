#ifndef OPENMW_PROCESSORPLAYERCHARGEN_HPP
#define OPENMW_PROCESSORPLAYERCHARGEN_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerCharGen final: public PlayerProcessor
    {
    public:
        ProcessorPlayerCharGen()
        {
            BPP_INIT(ID_PLAYER_CHARGEN)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (!isLocal()) return;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_CHARGEN from server (stage %d/%d)",
                player->charGenState.currentStage, player->charGenState.endStage);
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERCHARGEN_HPP
