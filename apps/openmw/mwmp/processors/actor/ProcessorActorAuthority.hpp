#ifndef OPENMW_PROCESSORACTORAUTHORITY_HPP
#define OPENMW_PROCESSORACTORAUTHORITY_HPP


#include "../ActorProcessor.hpp"
#include <components/detournavigator/navigator.hpp>
#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/CellController.hpp"

namespace mwmp
{
    class ProcessorActorAuthority final: public ActorProcessor
    {
    public:
        ProcessorActorAuthority()
        {
            BPP_INIT(ID_ACTOR_AUTHORITY)
        }

        void Do(ActorPacket &packet, ActorList &actorList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s about %s", strPacketID.c_str(), actorList.cell.getShortDescription().c_str());
            mwmp::CellController *cellController = Main::get().getCellController();

            // Never initialize LocalActors in a cell that is no longer loaded, if the server's packet arrived too late
            if (cellController->isActiveWorldCell(actorList.cell))
            {
                cellController->initializeCell(actorList.cell);
                mwmp::Cell *cell = cellController->getCell(actorList.cell);
                if (!cell)
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "ProcessorActorAuthority: getCell returned nullptr for %s", actorList.cell.getShortDescription().c_str());
                    return;
                }
                cell->setAuthority(guid);

                if (isLocal())
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- The new authority is me");
                    cell->uninitializeDedicatedActors();
                    cell->initializeLocalActors();
                    cell->updateLocal(true);

                    // Enable updates for DetourNavigator for advanced pathfinding
                    MWBase::World* world = MWBase::Environment::get().getWorld();
                    world->getNavigator()->update(world->getPlayerPtr().getRefData().getPosition().asVec3(), nullptr);
                }
                else
                {
                    BasePlayer *player = PlayerList::getPlayer(guid);

                    if (player != nullptr)
                        LOG_APPEND(TimedLog::LOG_INFO, "- The new authority is %s", player->npc.mName.c_str());

                    cell->uninitializeLocalActors();
                }
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Ignoring it because that cell isn't loaded");
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORAUTHORITY_HPP
