#ifndef OPENMW_PROCESSORPLAYERSTATSDYNAMIC_HPP
#define OPENMW_PROCESSORPLAYERSTATSDYNAMIC_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerStatsDynamic final: public PlayerProcessor
    {
    public:
        ProcessorPlayerStatsDynamic()
        {
            BPP_INIT(ID_PLAYER_STATS_DYNAMIC)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                if (isRequest())
                    static_cast<LocalPlayer *>(player)->updateStatsDynamic(true);
                else
                    static_cast<LocalPlayer *>(player)->setDynamicStats();
            }
            else if (player != nullptr)
            {
                static_cast<DedicatedPlayer*>(player)->setStatsDynamic();
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERSTATSDYNAMIC_HPP
