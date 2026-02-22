#ifndef OPENMW_PROCESSORPLAYERBEHAVIOR_HPP
#define OPENMW_PROCESSORPLAYERBEHAVIOR_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerBehavior final: public PlayerProcessor
    {
    public:
        ProcessorPlayerBehavior()
        {
            BPP_INIT(ID_PLAYER_BEHAVIOR)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                //static_cast<LocalPlayer *>(player)->setBehavior();
            }
            else if (player != nullptr)
            {
                //static_cast<DedicatedPlayer *>(player)->setBehavior();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERBEHAVIOR_HPP
