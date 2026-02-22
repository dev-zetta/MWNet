#ifndef OPENMW_PROCESSORPLAYERSKILL_HPP
#define OPENMW_PROCESSORPLAYERSKILL_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerSkill final: public PlayerProcessor
    {
    public:
        ProcessorPlayerSkill()
        {
            BPP_INIT(ID_PLAYER_SKILL)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                if (isRequest())
                    static_cast<LocalPlayer *>(player)->updateSkills(true);
                else
                    static_cast<LocalPlayer *>(player)->setSkills();
            }
            else if (player != nullptr)
            {
                static_cast<DedicatedPlayer *>(player)->setSkills();
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERSKILL_HPP
