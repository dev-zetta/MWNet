#ifndef OPENMW_PROCESSORPLAYERREPUTATION_HPP
#define OPENMW_PROCESSORPLAYERREPUTATION_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerReputation final: public PlayerProcessor
    {
    public:
        ProcessorPlayerReputation()
        {
            BPP_INIT(ID_PLAYER_REPUTATION)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isRequest())
            {
                static_cast<LocalPlayer *>(player)->updateReputation(true);
            }
            else if (player != nullptr)
            {
                static_cast<LocalPlayer *>(player)->setReputation();
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERREPUTATION_HPP
