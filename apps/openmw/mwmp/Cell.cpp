#include <components/esm3/cellid.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/livecellref.hpp"
#include "../mwworld/worldimp.hpp"

#include "Cell.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"

using namespace mwmp;

mwmp::Cell::Cell(MWWorld::CellStore* cellStore)
{
    store = cellStore;
    shouldInitializeActors = false;

    updateTimer = 0;
}

Cell::~Cell()
{

}

void Cell::updateLocal(bool forceUpdate)
{
    if (localActors.empty())
        return;

    const float timeoutSec = 0.025;

    if (!forceUpdate && (updateTimer += MWBase::Environment::get().getFrameDuration()) < timeoutSec)
        return;
    else
        updateTimer = 0;

    CellController *cellController = Main::get().getCellController();
    ActorList *actorList = mwmp::Main::get().getNetworking()->getActorList();
    actorList->reset();

    // The client can become the provisional local authority before the server's
    // lease grant arrives. Do not emit unleased actor state during that window.
    if (!actorList->setCell(store->getCell()->getEsm3()))
        return;

    for (auto it = localActors.begin(); it != localActors.end();)
    {
        LocalActor *actor = it->second.get();

        MWWorld::CellStore *newStore = actor->getPtr().getCell();

        if (newStore != store)
        {
            actor->updateCell();
            std::string mapIndex = it->first;

            // If the cell this actor has moved to is under our authority, move them to it
            if (cellController->hasLocalAuthority(actor->cell))
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moving LocalActor %s to our authority in %s",
                    mapIndex.c_str(), actor->cell.getShortDescription().c_str());
                Cell *newCell = cellController->getCell(actor->cell);
                if (newCell)
                {
                    newCell->localActors.insert_or_assign(mapIndex, std::move(it->second));
                    cellController->setLocalActorRecord(mapIndex, newCell->getShortDescription());
                }
                else
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Cell::updateLocal: getCell nullptr for actor %s moving to %s", mapIndex.c_str(), actor->cell.getShortDescription().c_str());
                    cellController->removeLocalActorRecord(mapIndex);
                }
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting LocalActor %s which is no longer under our authority",
                    mapIndex.c_str(), getShortDescription().c_str());
                cellController->removeLocalActorRecord(mapIndex);
            }

            it = localActors.erase(it);
        }
        else
        {
            if (actor->getPtr().getRefData().isEnabled())
            {
                if (actor->getPtr().getRefData().isDeletedByContentFile())
                {
                    std::string mapIndex = it->first;
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting LocalActor %s whose reference has been deleted",
                        mapIndex.c_str(), getShortDescription().c_str());
                    cellController->removeLocalActorRecord(mapIndex);
                    it = localActors.erase(it);
                    continue;
                }
                else
                {
                    // Forcibly update this local actor if its data has never been sent before;
                    // otherwise, use the current forceUpdate value
                    actor->update(actor->hasSentData ? forceUpdate : true);
                }
            }

            ++it;
        }
    }

    actorList->sendPositionActors();
    actorList->sendAnimFlagsActors();
    actorList->sendAnimPlayActors();
    actorList->sendSpeechActors();
    actorList->sendDeathActors();
    actorList->sendStatsDynamicActors();
    actorList->sendEquipmentActors();
    actorList->sendAttackActors();
    actorList->sendCastActors();
    actorList->sendCellChangeActors();
}

void Cell::updateDedicated(float dt)
{
    if (dedicatedActors.empty()) return;
    
    for (auto &actor : dedicatedActors)
        actor.second->update(dt);

    // Are we the authority over this cell? If so, uninitialize DedicatedActors
    // after the above update
    if (hasLocalAuthority())
        uninitializeDedicatedActors();
}

void Cell::readPositions(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;
    
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->position = baseActor.position;
            actor->direction = baseActor.direction;

            if (!actor->hasPositionData)
            {
                actor->hasPositionData = true;

                // If this is our first packet about this actor's position, force an update
                // now instead of waiting for its frame
                //
                // That way, if this actor is about to become a LocalActor, initial data about it
                // received from the server still gets set
                actor->setPosition();
            }
        }
    }
}

void Cell::readAnimFlags(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->movementFlags = baseActor.movementFlags;
            actor->drawState = baseActor.drawState;
            actor->isFlying = baseActor.isFlying;
        }
    }
}

void Cell::readAnimPlay(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->animation.groupname = baseActor.animation.groupname;
            actor->animation.mode = baseActor.animation.mode;
            actor->animation.count = baseActor.animation.count;
            actor->animation.persist = baseActor.animation.persist;
            actor->playAnimation();
        }
    }
}

void Cell::readStatsDynamic(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty() && localActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (localActors.count(mapIndex) > 0)
        {
            LocalActor *actor = getLocalActor(mapIndex);
            actor->creatureStats = baseActor.creatureStats;
            actor->hasStatsDynamicData = true;

            MWWorld::Ptr ptr = actor->getPtr();
            MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
            if (actor->creatureStats.mDynamic[0].mCurrent > 0)
                MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);
            for (int index = 0; index < 3; ++index)
            {
                MWMechanics::DynamicStat<float> value;
                value.readState(actor->creatureStats.mDynamic[index]);
                stats.setDynamic(index, value);
            }
        }
        else if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->creatureStats = baseActor.creatureStats;

            if (!actor->hasStatsDynamicData)
            {
                actor->hasStatsDynamicData = true;

                // If this is our first packet about this actor's dynamic stats, force an update
                // now instead of waiting for its frame
                //
                // That way, if this actor is about to become a LocalActor, initial data about it
                // received from the server still gets set
                actor->setStatsDynamic();
            }
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readDeath(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty() && localActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        LocalActor* localActor = localActors.count(mapIndex) > 0
            ? getLocalActor(mapIndex) : nullptr;
        DedicatedActor* dedicatedActor = dedicatedActors.count(mapIndex) > 0
            ? getDedicatedActor(mapIndex) : nullptr;
        if (localActor != nullptr || dedicatedActor != nullptr)
        {
            BaseActor *actor = localActor != nullptr
                ? static_cast<BaseActor*>(localActor)
                : static_cast<BaseActor*>(dedicatedActor);
            actor->creatureStats.mDead = true;
            actor->creatureStats.mDynamic[0].mCurrent = 0;

            MWWorld::Ptr actorPtr = localActor != nullptr
                ? localActor->getPtr() : dedicatedActor->getPtr();

            Main::get().getCellController()->setQueuedDeathState(actorPtr, baseActor.deathState);

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_ACTOR_DEATH about %s %i-%i in cell %s\n- deathState: %d\n-isInstantDeath: %s",
                actor->refId.c_str(), actor->refNum, actor->mpNum, getShortDescription().c_str(),
                baseActor.deathState, baseActor.isInstantDeath ? "true" : "false");

            if (baseActor.isInstantDeath)
            {
                actorPtr.getClass().getCreatureStats(actorPtr).setDeathAnimationFinished(true);
                MWBase::Environment::get().getWorld()->enableActorCollision(actorPtr, false);
            }
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readEquipment(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = getDedicatedActor(mapIndex);

            for (int slot = 0; slot < 19; ++slot)
                actor->equipmentItems[slot] = baseActor.equipmentItems[slot];

            actor->setEquipment();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readSpeech(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->sound = baseActor.sound;
            actor->playSound();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readSpellsActive(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto& baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor* actor = getDedicatedActor(mapIndex);
            actor->spellsActiveChanges = baseActor.spellsActiveChanges;

            int spellsActiveAction = baseActor.spellsActiveChanges.action;

            if (spellsActiveAction == SpellsActiveChanges::ADD)
                actor->addSpellsActive();
            else if (spellsActiveAction == SpellsActiveChanges::REMOVE)
                actor->removeSpellsActive();
            else
                actor->setSpellsActive();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readAi(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->aiAction = baseActor.aiAction;
            actor->aiDistance = baseActor.aiDistance;
            actor->aiDuration = baseActor.aiDuration;
            actor->aiShouldRepeat = baseActor.aiShouldRepeat;
            actor->aiCoordinates = baseActor.aiCoordinates;
            actor->hasAiTarget = baseActor.hasAiTarget;
            actor->aiTarget = baseActor.aiTarget;
            actor->setAi();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readAttack(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Reading ActorAttack about %s", mapIndex.c_str());

            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->attack = baseActor.attack;

            MechanicsHelper::processAttack(actor->attack, actor->getPtr());
        }
    }
}

void Cell::readCast(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Reading ActorCast about %s", mapIndex.c_str());

            DedicatedActor *actor = getDedicatedActor(mapIndex);
            actor->cast = baseActor.cast;

            // Set the correct drawState here if we've somehow we've missed a previous
            // AnimFlags packet
            if (actor->drawState != static_cast<char>(MWMechanics::DrawState::Spell))
            {
                actor->drawState = static_cast<char>(MWMechanics::DrawState::Spell);
                actor->setAnimFlags();
            }

            MechanicsHelper::processCast(actor->cast, actor->getPtr());
        }
    }
}

void Cell::readCellChange(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    CellController *cellController = Main::get().getCellController();

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        // Is a packet mistakenly moving the actor to the cell it's already in? If so, ignore it
        if (Misc::StringUtils::ciEqual(getShortDescription(), baseActor.cell.getShortDescription()))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Server says DedicatedActor %s moved to %s, but it was already there",
                mapIndex.c_str(), getShortDescription().c_str());
            continue;
        }

        if (dedicatedActors.count(mapIndex) > 0)
        {
            auto actorIt = dedicatedActors.find(mapIndex);
            DedicatedActor *dedicatedActor = actorIt->second.get();
            dedicatedActor->cell = baseActor.cell;
            dedicatedActor->position = baseActor.position;
            dedicatedActor->direction = baseActor.direction;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Server says DedicatedActor %s moved to %s",
                mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());

            MWWorld::CellStore *newStore = cellController->getCellStore(dedicatedActor->cell);
            dedicatedActor->setCell(newStore);

            // If the cell this actor has moved to is active and not under our authority, move them to it
            if (cellController->isActiveWorldCell(dedicatedActor->cell) && !cellController->hasLocalAuthority(dedicatedActor->cell))
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moving DedicatedActor %s to our active cell %s",
                    mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());
                cellController->initializeCell(dedicatedActor->cell);
                Cell *newCell = cellController->getCell(dedicatedActor->cell);
                if (newCell)
                {
                    newCell->dedicatedActors.insert_or_assign(mapIndex, std::move(actorIt->second));
                    cellController->setDedicatedActorRecord(mapIndex, newCell->getShortDescription());
                }
                else
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Cell::updateDedicated: getCell nullptr for DedicatedActor %s moving to %s", mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());
                    cellController->removeDedicatedActorRecord(mapIndex);
                }
            }
            else
            {
                if (cellController->hasLocalAuthority(dedicatedActor->cell))
                {
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Creating new LocalActor based on %s in %s",
                        mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());
                    Cell *newCell = cellController->getCell(dedicatedActor->cell);
                    if (newCell)
                    {
                        auto localActor = std::make_unique<LocalActor>();
                        localActor->cell = dedicatedActor->cell;
                        localActor->setPtr(dedicatedActor->getPtr());
                        localActor->position = dedicatedActor->position;
                        localActor->direction = dedicatedActor->direction;
                        localActor->movementFlags = dedicatedActor->movementFlags;
                        localActor->drawState = dedicatedActor->drawState;
                        localActor->isFlying = dedicatedActor->isFlying;
                        localActor->creatureStats = dedicatedActor->creatureStats;

                        newCell->localActors.insert_or_assign(mapIndex, std::move(localActor));
                        cellController->setLocalActorRecord(mapIndex, newCell->getShortDescription());
                    }
                    else
                        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Cell::uninitializeDedicatedActors: getCell nullptr for %s in %s", mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());
                }

                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting DedicatedActor %s which is no longer needed",
                    mapIndex.c_str(), getShortDescription().c_str());
                cellController->removeDedicatedActorRecord(mapIndex);
            }

            dedicatedActors.erase(actorIt);
        }
    }
}

void Cell::initializeLocalActor(const MWWorld::Ptr& ptr)
{
    std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);
    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Initializing LocalActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());

    auto actor = std::make_unique<LocalActor>();
    actor->cell = store->getCell()->getEsm3();
    actor->setPtr(ptr);

    localActors.insert_or_assign(mapIndex, std::move(actor));

    Main::get().getCellController()->setLocalActorRecord(mapIndex, getShortDescription());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized LocalActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());
}

void Cell::initializeLocalActors()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Initializing LocalActors in %s", getShortDescription().c_str());

    for (const auto &mergedRef : store->getMergedRefs())
    {
        if (mergedRef->mClass->isActor())
        {
            MWWorld::Ptr ptr(mergedRef, store);

            // If this Ptr is lacking a unique index, ignore it
            if (ptr.getCellRef().getRefNum().mIndex == 0 && ptr.getCellRef().getMpNum() == 0) continue;

            // If this Ptr is disabled or deleted, ignore it
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) continue;

            std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);

            // Only initialize this actor if it isn't already initialized
            if (localActors.count(mapIndex) == 0)
                initializeLocalActor(ptr);
        }
    }

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized LocalActors in %s", getShortDescription().c_str());
}

void Cell::initializeDedicatedActor(const MWWorld::Ptr& ptr)
{
    std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);
    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Initializing DedicatedActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());

    auto actor = std::make_unique<DedicatedActor>();
    actor->cell = store->getCell()->getEsm3();
    actor->setPtr(ptr);

    dedicatedActors.insert_or_assign(mapIndex, std::move(actor));

    Main::get().getCellController()->setDedicatedActorRecord(mapIndex, getShortDescription());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized DedicatedActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());
}

void Cell::initializeDedicatedActors(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        // If this key doesn't exist, create it
        if (dedicatedActors.count(mapIndex) == 0)
        {
            MWWorld::Ptr ptrFound = store->searchExact(baseActor.refNum, baseActor.mpNum, ESM::RefId::stringRefId(baseActor.refId), true);

            if (ptrFound.isEmpty()) continue;

            initializeDedicatedActor(ptrFound);
        }
    }
}

void Cell::uninitializeLocalActors()
{
    for (const auto &actor : localActors)
        Main::get().getCellController()->removeLocalActorRecord(actor.first);

    localActors.clear();
}

void Cell::uninitializeDedicatedActors(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);
        auto it = dedicatedActors.find(mapIndex);
        if (it == dedicatedActors.end())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Cell::uninitializeDedicatedActors: actor %s not found in cell (may be a local actor)", mapIndex.c_str());
            continue;
        }
        Main::get().getCellController()->removeDedicatedActorRecord(mapIndex);
        dedicatedActors.erase(it);
    }
}

void Cell::uninitializeDedicatedActors()
{
    for (const auto &actor : dedicatedActors)
        Main::get().getCellController()->removeDedicatedActorRecord(actor.first);

    dedicatedActors.clear();
}

LocalActor *Cell::getLocalActor(std::string actorIndex)
{
    auto it = localActors.find(actorIndex);
    if (it == localActors.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Cell::getLocalActor: actor %s not found in cell", actorIndex.c_str());
        return nullptr;
    }
    return it->second.get();
}

DedicatedActor *Cell::getDedicatedActor(std::string actorIndex)
{
    auto it = dedicatedActors.find(actorIndex);
    if (it == dedicatedActors.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Cell::getDedicatedActor: actor %s not found in cell", actorIndex.c_str());
        return nullptr;
    }
    return it->second.get();
}

bool Cell::hasLocalAuthority()
{
    return authorityGuid == Main::get().getLocalPlayer()->guid;
}

void Cell::setAuthority(const RakNet::RakNetGUID& guid, std::uint64_t leaseId)
{
    authorityGuid = guid;
    authorityLeaseId = leaseId;
}

std::uint64_t Cell::getAuthorityLeaseId() const
{
    return authorityLeaseId;
}

MWWorld::CellStore *Cell::getCellStore()
{
    return store;
}

std::string Cell::getShortDescription()
{
    return std::string(store->getCell()->getShortDescription());
}
