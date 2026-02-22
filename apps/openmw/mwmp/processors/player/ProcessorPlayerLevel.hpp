#ifndef OPENMW_PROCESSORPLAYERLEVEL_HPP
#define OPENMW_PROCESSORPLAYERLEVEL_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerLevel final: public PlayerProcessor
    {
    public:
        ProcessorPlayerLevel()
        {
            BPP_INIT(ID_PLAYER_LEVEL)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            if (isLocal())
            {
                if (isRequest())
                    static_cast<LocalPlayer *>(player)->updateLevel(true);
                else
                    static_cast<LocalPlayer *>(player)->setLevel();
            }
            else if (player != nullptr)
            {
                MWWorld::Ptr ptrPlayer =  static_cast<DedicatedPlayer *>(player)->getPtr();
                MWMechanics::CreatureStats *ptrCreatureStats = &ptrPlayer.getClass().getCreatureStats(ptrPlayer);

                ptrCreatureStats->setLevel(player->creatureStats.mLevel);
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERLEVEL_HPP
