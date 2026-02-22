#ifndef OPENMW_PROCESSORPLAYERANIMFLAGS_HPP
#define OPENMW_PROCESSORPLAYERANIMFLAGS_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerAnimFlags final: public PlayerProcessor
    {
    public:
        ProcessorPlayerAnimFlags()
        {
            BPP_INIT(ID_PLAYER_ANIM_FLAGS)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                if (isRequest())
                    static_cast<LocalPlayer *>(player)->updateAnimFlags(true);
            }
            else if (player != nullptr)
                static_cast<DedicatedPlayer *>(player)->setAnimFlags();
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERANIMFLAGS_HPP
