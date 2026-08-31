#include "Cell.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include "Networking.hpp"
#include "Player.hpp"
#include "Script/Script.hpp"

Cell::Cell(ESM::Cell cell) : cell(cell)
{
    cellActorList.count = 0;
}

Cell::Iterator Cell::begin() const
{
    return players.begin();
}

Cell::Iterator Cell::end() const
{
    return players.end();
}

void Cell::addPlayer(Player *player)
{
    // Ensure the player hasn't already been added
    auto it = find(begin(), end(), player);

    if (it != end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Attempt to add %s to Cell %s again was ignored", player->npc.mName.c_str(), getShortDescription().c_str());
        return;
    }

    auto it2 = find(player->cells.begin(), player->cells.end(), this);
    if (it2 == player->cells.end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Adding %s to Player %s", getShortDescription().c_str(), player->npc.mName.c_str());

        player->cells.push_back(this);
    }

    LOG_APPEND(TimedLog::LOG_INFO, "- Adding %s to Cell %s", player->npc.mName.c_str(), getShortDescription().c_str());

    Script::Call<Script::CallbackIdentity("OnCellLoad")>(player->getId(), getShortDescription().c_str());

    players.push_back(player);
}

void Cell::removePlayer(Player *player, bool cleanPlayer)
{
    for (Iterator it = begin(); it != end(); it++)
    {
        if (*it == player)
        {
            if (authorityGuid == player->guid && authorityLeaseId != 0)
            {
                if (mwmp::Networking* networking = mwmp::Networking::getPtr())
                    networking->releaseActorAuthority(cell, authorityGuid, authorityLeaseId);
                clearAuthority();
            }

            if (cleanPlayer)
            {
                auto it2 = find(player->cells.begin(), player->cells.end(), this);
                if (it2 != player->cells.end())
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Removing %s from Player %s", getShortDescription().c_str(), player->npc.mName.c_str());

                    player->cells.erase(it2);
                }
            }

            LOG_APPEND(TimedLog::LOG_INFO, "- Removing %s from Cell %s", player->npc.mName.c_str(), getShortDescription().c_str());

            Script::Call<Script::CallbackIdentity("OnCellUnload")>(player->getId(), getShortDescription().c_str());

            players.erase(it);
            return;
        }
    }
}

void Cell::readActorList(unsigned char packetID, const mwmp::BaseActorList *newActorList)
{
    if (packetID == ID_ACTOR_LIST)
    {
        if (newActorList->action == mwmp::BaseActorList::REQUEST)
            return;
        if (newActorList->action == mwmp::BaseActorList::REMOVE)
        {
            removeActors(newActorList);
            return;
        }
        if (newActorList->action == mwmp::BaseActorList::SET)
        {
            std::vector<mwmp::BaseActor> replacement;
            replacement.reserve(newActorList->baseActors.size());
            for (const mwmp::BaseActor& actor : newActorList->baseActors)
            {
                if (mwmp::BaseActor* existing = getActor(actor.refNum, actor.mpNum))
                {
                    replacement.push_back(*existing);
                    replacement.back().refId = actor.refId;
                }
                else
                    replacement.push_back(actor);
            }
            cellActorList.baseActors = std::move(replacement);
            cellActorList.count = cellActorList.baseActors.size();
            rebuildActorIndex();
            return;
        }

        for (const mwmp::BaseActor& actor : newActorList->baseActors)
        {
            if (mwmp::BaseActor* existing = getActor(actor.refNum, actor.mpNum))
                existing->refId = actor.refId;
            else
            {
                cellActorList.baseActors.push_back(actor);
                actorIndexes.insert_or_assign(actorKey(actor.refNum, actor.mpNum),
                    cellActorList.baseActors.size() - 1);
            }
        }
        cellActorList.count = cellActorList.baseActors.size();
        return;
    }

    for (unsigned int i = 0; i < newActorList->count; i++)
    {
        mwmp::BaseActor newActor = newActorList->baseActors.at(i);
        mwmp::BaseActor *cellActor;

        if (containsActor(newActor.refNum, newActor.mpNum))
        {
            cellActor = getActor(newActor.refNum, newActor.mpNum);

            switch (packetID)
            {
            case ID_ACTOR_POSITION:

                cellActor->hasPositionData = true;
                cellActor->position = newActor.position;
                break;

            case ID_ACTOR_STATS_DYNAMIC:

                cellActor->hasStatsDynamicData = true;
                cellActor->creatureStats.mDynamic[0] = newActor.creatureStats.mDynamic[0];
                cellActor->creatureStats.mDynamic[1] = newActor.creatureStats.mDynamic[1];
                cellActor->creatureStats.mDynamic[2] = newActor.creatureStats.mDynamic[2];
                break;

            case ID_ACTOR_EQUIPMENT:
                for (std::size_t slot = 0; slot < 19; ++slot)
                    cellActor->equipmentItems[slot] = newActor.equipmentItems[slot];
                break;

            case ID_ACTOR_AI:
                cellActor->aiAction = newActor.aiAction;
                cellActor->hasAiTarget = newActor.hasAiTarget;
                cellActor->aiTarget = newActor.aiTarget;
                cellActor->aiCoordinates = newActor.aiCoordinates;
                cellActor->aiDistance = newActor.aiDistance;
                cellActor->aiDuration = newActor.aiDuration;
                cellActor->aiShouldRepeat = newActor.aiShouldRepeat;
                break;
            }
        }
        else
        {
            cellActorList.baseActors.push_back(newActor);
            actorIndexes.insert_or_assign(actorKey(newActor.refNum, newActor.mpNum),
                cellActorList.baseActors.size() - 1);
        }
    }

    cellActorList.count = cellActorList.baseActors.size();
}

bool Cell::containsActor(int refNum, int mpNum) const
{
    return actorIndexes.contains(actorKey(refNum, mpNum));
}

mwmp::BaseActor *Cell::getActor(int refNum, int mpNum)
{
    const auto found = actorIndexes.find(actorKey(refNum, mpNum));
    if (found == actorIndexes.end() || found->second >= cellActorList.baseActors.size())
        return nullptr;
    return &cellActorList.baseActors[found->second];
}

void Cell::removeActors(const mwmp::BaseActorList *newActorList)
{
    std::unordered_set<std::uint64_t> removals;
    removals.reserve(newActorList->baseActors.size());
    for (const mwmp::BaseActor& actor : newActorList->baseActors)
        removals.insert(actorKey(actor.refNum, actor.mpNum));

    std::erase_if(cellActorList.baseActors,
        [&removals](const mwmp::BaseActor& actor) {
            return removals.contains(actorKey(actor.refNum, actor.mpNum));
        });

    cellActorList.count = cellActorList.baseActors.size();
    rebuildActorIndex();
}

Cell::PreparedActorRoster Cell::prepareActorRoster(
    std::vector<mwmp::BaseActor> actors) const
{
    PreparedActorRoster result;
    result.actors = std::move(actors);
    result.indexes.reserve(result.actors.size());
    for (std::size_t index = 0; index < result.actors.size(); ++index)
    {
        const mwmp::BaseActor& actor = result.actors[index];
        if (!result.indexes.emplace(actorKey(actor.refNum, actor.mpNum), index).second)
            throw std::invalid_argument("actor roster contains a duplicate identity");
    }
    return result;
}

void Cell::commitActorRoster(PreparedActorRoster&& roster) noexcept
{
    cellActorList.baseActors.swap(roster.actors);
    actorIndexes.swap(roster.indexes);
    cellActorList.count = static_cast<unsigned int>(cellActorList.baseActors.size());
}

std::uint64_t Cell::actorKey(std::uint32_t refNum, std::uint32_t mpNum) noexcept
{
    return (static_cast<std::uint64_t>(refNum) << 32)
        | static_cast<std::uint64_t>(mpNum);
}

void Cell::rebuildActorIndex()
{
    actorIndexes.clear();
    actorIndexes.reserve(cellActorList.baseActors.size());
    for (std::size_t index = 0; index < cellActorList.baseActors.size(); ++index)
    {
        const mwmp::BaseActor& actor = cellActorList.baseActors[index];
        actorIndexes.insert_or_assign(actorKey(actor.refNum, actor.mpNum), index);
    }
}

mwmp::transport::TransportConnectionId *Cell::getAuthority()
{
    return &authorityGuid;
}

void Cell::setAuthority(const mwmp::transport::TransportConnectionId& guid, std::uint64_t leaseId)
{
    authorityGuid = guid;
    authorityLeaseId = leaseId;
}

void Cell::clearAuthority()
{
    authorityGuid = mwmp::transport::TransportConnectionId{};
    authorityLeaseId = 0;
}

std::uint64_t Cell::getAuthorityLeaseId() const
{
    return authorityLeaseId;
}

mwmp::BaseActorList *Cell::getActorList()
{
    return &cellActorList;
}

Cell::TPlayers Cell::getPlayers() const
{
    return players;
}

void Cell::sendToLoaded(mwmp::ActorPacket *actorPacket, mwmp::BaseActorList *baseActorList) const
{
    if (players.empty())
        return;

    std::list <Player*> plList;

    for (auto pl : players)
    {
        if (pl != nullptr && !pl->npc.mName.empty())
            plList.push_back(pl);
    }

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl->guid == baseActorList->guid) continue;

        actorPacket->setActorList(baseActorList);

        // Send the packet to this eligible guid
        actorPacket->Send(pl->guid);
    }
}

void Cell::sendToLoaded(mwmp::ObjectPacket *objectPacket, mwmp::BaseObjectList *baseObjectList) const
{
    if (players.empty())
        return;

    std::list <Player*> plList;

    for (auto pl : players)
    {
        if (pl != nullptr && !pl->npc.mName.empty())
            plList.push_back(pl);
    }

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl->guid == baseObjectList->guid) continue;

        objectPacket->setObjectList(baseObjectList);

        // Send the packet to this eligible guid
        objectPacket->Send(pl->guid);
    }
}

std::string Cell::getShortDescription() const
{
    return cell.getShortDescription();
}
