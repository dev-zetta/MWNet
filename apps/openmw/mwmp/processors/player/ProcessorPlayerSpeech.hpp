#ifndef OPENMW_PROCESSORPLAYERSPEECH_HPP
#define OPENMW_PROCESSORPLAYERSPEECH_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerSpeech final: public PlayerProcessor
    {
    public:
        ProcessorPlayerSpeech()
        {
            BPP_INIT(ID_PLAYER_SPEECH)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_SPEECH about LocalPlayer from server");
                static_cast<LocalPlayer*>(player)->playSpeech();
            }
            else if (player != nullptr)
                static_cast<DedicatedPlayer*>(player)->playSpeech();
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERSPEECH_HPP
