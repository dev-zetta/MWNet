#ifndef OPENMW_PROCESSORPLAYERATTRIBUTE_HPP
#define OPENMW_PROCESSORPLAYERATTRIBUTE_HPP


#include "../PlayerProcessor.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwclass/npc.hpp"

namespace mwmp
{
    class ProcessorPlayerAttribute final: public PlayerProcessor
    {
    public:
        ProcessorPlayerAttribute()
        {
            BPP_INIT(ID_PLAYER_ATTRIBUTE)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                if (isRequest())
                    static_cast<LocalPlayer *>(player)->updateAttributes(true);
                else
                    static_cast<LocalPlayer *>(player)->setAttributes();
            }
            else if (player != nullptr)
            {
                static_cast<DedicatedPlayer *>(player)->setAttributes();
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERATTRIBUTE_HPP
