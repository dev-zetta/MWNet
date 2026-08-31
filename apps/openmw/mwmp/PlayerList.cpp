#include <components/openmw-mp/TimedLog.hpp>
#include <apps/openmw/mwclass/creature.hpp>

#include "../mwbase/environment.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"

#include "PlayerList.hpp"
#include "Main.hpp"
#include "DedicatedPlayer.hpp"
#include "CellController.hpp"
#include "GUIController.hpp"


using namespace mwmp;

std::map<mwmp::transport::TransportConnectionId, std::unique_ptr<DedicatedPlayer>> PlayerList::playerList;

void PlayerList::update(float dt)
{
    for (auto &playerEntry : playerList)
    {
        playerEntry.second->update(dt);
    }
}

DedicatedPlayer *PlayerList::newPlayer(mwmp::transport::TransportConnectionId guid)
{
    if (const auto existing = playerList.find(guid); existing != playerList.end())
        return existing->second.get();

    LOG_APPEND(TimedLog::LOG_INFO, "- Creating new DedicatedPlayer with connection %llu",
        static_cast<unsigned long long>(guid.value));

    std::unique_ptr<DedicatedPlayer> player(new DedicatedPlayer(guid));
    DedicatedPlayer* result = player.get();
    playerList.insert_or_assign(guid, std::move(player));

    LOG_APPEND(TimedLog::LOG_INFO, "- There are now %zu DedicatedPlayers", playerList.size());

    return result;
}

void PlayerList::deletePlayer(mwmp::transport::TransportConnectionId guid)
{
    const auto player = playerList.find(guid);
    if (player == playerList.end())
        return;

    if (player->second->reference)
        player->second->deleteReference();
    playerList.erase(player);
}

void PlayerList::cleanUp()
{
    playerList.clear();
}

DedicatedPlayer *PlayerList::getPlayer(mwmp::transport::TransportConnectionId guid)
{
    const auto player = playerList.find(guid);
    return player == playerList.end() ? nullptr : player->second.get();
}

DedicatedPlayer *PlayerList::getPlayer(const MWWorld::Ptr &ptr)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second->getPtr().mRef == nullptr)
            continue;
        
        ESM::RefId refId = ptr.getCellRef().getRefId();
        
        if (playerEntry.second->getPtr().getCellRef().getRefId() == refId)
            return playerEntry.second.get();
    }

    return nullptr;
}

DedicatedPlayer* PlayerList::getPlayer(int actorId)
{
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWWorld::Ptr playerPtr = playerEntry.second->getPtr();
        int playerActorId = playerPtr.getClass().getCreatureStats(playerPtr).getActorId();

        if (actorId == playerActorId)
            return playerEntry.second.get();
    }

    return nullptr;
}

std::vector<mwmp::transport::TransportConnectionId> PlayerList::getPlayersInCell(const ESM::Cell& cell)
{
    std::vector<mwmp::transport::TransportConnectionId> playersInCell;

    for (auto& playerEntry : playerList)
    {
        if (playerEntry.first != mwmp::transport::TransportConnectionId{})
        {
            if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
            {
                playersInCell.push_back(playerEntry.first);
            }
        }
    }

    return playersInCell;
}

bool PlayerList::isDedicatedPlayer(const MWWorld::Ptr &ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    // Players always have 0 as their refNum and mpNum
    if (ptr.getCellRef().getRefNum().mIndex != 0 || ptr.getCellRef().getMpNum() != 0)
        return false;

    return (getPlayer(ptr) != nullptr);
}

void PlayerList::enableMarkers(const ESM::Cell& cell)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
        {
            playerEntry.second->enableMarker();
        }
    }
}

/*
    Go through all DedicatedPlayers checking if their mHitAttemptActorId matches this one
    and set it to -1 if it does

    This resets the combat target for a DedicatedPlayer's followers in Actors::update()
*/
void PlayerList::clearHitAttemptActorId(int actorId)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWMechanics::CreatureStats &playerCreatureStats = playerEntry.second->getPtr().getClass().getCreatureStats(playerEntry.second->getPtr());

        (void)actorId; // actorId-based lookup replaced by RefNum in master; no-op for now
    }
}
