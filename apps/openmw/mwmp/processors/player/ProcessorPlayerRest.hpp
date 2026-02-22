#ifndef OPENMW_PROCESSORPLAYERREST_HPP
#define OPENMW_PROCESSORPLAYERREST_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerRest final: public PlayerProcessor
    {
    public:
        ProcessorPlayerRest()
        {
            BPP_INIT(ID_PLAYER_REST)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            // Placeholder to be filled in later
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERREST_HPP
