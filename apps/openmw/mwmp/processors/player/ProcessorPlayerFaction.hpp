#ifndef OPENMW_PROCESSORPLAYERFACTION_HPP
#define OPENMW_PROCESSORPLAYERFACTION_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerFaction final: public PlayerProcessor
    {
    public:
        ProcessorPlayerFaction()
        {
            BPP_INIT(ID_PLAYER_FACTION)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isRequest())
            {
                // Entire faction membership cannot currently be requested from players
            }
            else if (player != nullptr)
            {
                static_cast<LocalPlayer*>(player)->setFactions();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERFACTION_HPP
