#ifndef OPENMW_PROCESSORPLAYERBOUNTY_HPP
#define OPENMW_PROCESSORPLAYERBOUNTY_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerBounty final: public PlayerProcessor
    {
    public:
        ProcessorPlayerBounty()
        {
            BPP_INIT(ID_PLAYER_BOUNTY)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                if (isRequest())
                    static_cast<LocalPlayer *>(player)->updateBounty(true);
                else
                    static_cast<LocalPlayer *>(player)->setBounty();
            }
            else if (player != nullptr)
            {
                MWWorld::Ptr ptrPlayer =  static_cast<DedicatedPlayer *>(player)->getPtr();
                MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);

                ptrNpcStats->setBounty(player->npcStats.mBounty);
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERBOUNTY_HPP
