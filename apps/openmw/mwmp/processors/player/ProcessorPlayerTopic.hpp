#ifndef OPENMW_PROCESSORPLAYERTOPIC_HPP
#define OPENMW_PROCESSORPLAYERTOPIC_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerTopic final: public PlayerProcessor
    {
    public:
        ProcessorPlayerTopic()
        {
            BPP_INIT(ID_PLAYER_TOPIC)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isRequest())
            {
                // Entire list of topics cannot currently be requested from players
            }
            else if (player != nullptr)
            {
                static_cast<LocalPlayer*>(player)->addTopics();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERTOPIC_HPP
