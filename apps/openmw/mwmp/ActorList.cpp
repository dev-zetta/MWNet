#include "ActorList.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"

#include "../mwworld/class.hpp"

#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

ActorList::ActorList()
{

}

ActorList::~ActorList()
{

}

Networking *ActorList::getNetworking()
{
    return mwmp::Main::get().getNetworking();
}

void ActorList::reset()
{
    cell.blank();
    authorityLeaseId = 0;
    authorityLeaseDurationMs = 0;
    baseActors.clear();
    positionActors.clear();
    animFlagsActors.clear();
    animPlayActors.clear();
    speechActors.clear();
    statsDynamicActors.clear();
    deathActors.clear();
    equipmentActors.clear();
    aiActors.clear();
    attackActors.clear();
    castActors.clear();
    cellChangeActors.clear();
    guid = mwmp::Main::get().getNetworking()->getLocalPlayer()->guid;
}

bool ActorList::setCell(const ESM::Cell& newCell)
{
    // Also called at send time by legacy immediate AI/combat paths.
    cell = newCell;
    authorityLeaseId = 0;
    CellController* controller = Main::get().getCellController();
    if (controller->isInitializedCell(newCell))
    {
        if (Cell* multiplayerCell = controller->getCell(newCell);
            multiplayerCell && multiplayerCell->hasUsableAuthority())
            authorityLeaseId = multiplayerCell->getAuthorityLeaseId();
    }
    return authorityLeaseId != 0;
}

void ActorList::addActor(BaseActor baseActor)
{
    baseActors.push_back(baseActor);
}

void ActorList::addPositionActor(BaseActor baseActor)
{
    positionActors.push_back(baseActor);
}

void ActorList::addAnimFlagsActor(BaseActor baseActor)
{
    animFlagsActors.push_back(baseActor);
}

void ActorList::addAnimPlayActor(BaseActor baseActor)
{
    animPlayActors.push_back(baseActor);
}

void ActorList::addSpeechActor(BaseActor baseActor)
{
    speechActors.push_back(baseActor);
}

void ActorList::addStatsDynamicActor(BaseActor baseActor)
{
    statsDynamicActors.push_back(baseActor);
}

void ActorList::addDeathActor(BaseActor baseActor)
{
    deathActors.push_back(baseActor);
}

void ActorList::addEquipmentActor(BaseActor baseActor)
{
    equipmentActors.push_back(baseActor);
}

void ActorList::addAiActor(BaseActor baseActor)
{
    aiActors.push_back(baseActor);
}

void ActorList::addAiActor(const MWWorld::Ptr& actorPtr, const MWWorld::Ptr& targetPtr, unsigned int aiAction)
{
    mwmp::BaseActor baseActor;
    baseActor.refNum = actorPtr.getCellRef().getRefNum().mIndex;
    baseActor.mpNum = actorPtr.getCellRef().getMpNum();
    baseActor.aiAction = aiAction;
    baseActor.aiTarget = MechanicsHelper::getTarget(targetPtr);
    baseActor.hasAiTarget = !targetPtr.isEmpty();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Preparing to send ID_ACTOR_AI about %s %i-%i\n- action: %i",
        actorPtr.getCellRef().getRefId().getRefIdString().c_str(), baseActor.refNum, baseActor.mpNum, aiAction);

    if (baseActor.aiTarget.isPlayer)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "- Has player target %s",
            targetPtr.getClass().getName(targetPtr).data());
    }
    else
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "- Has actor target %s %i-%i",
            targetPtr.getCellRef().getRefId().getRefIdString().c_str(), baseActor.aiTarget.refNum, baseActor.aiTarget.mpNum);
    }

    addAiActor(baseActor);
}

void ActorList::addAttackActor(BaseActor baseActor)
{
    attackActors.push_back(baseActor);
}

void ActorList::addAttackActor(const MWWorld::Ptr& actorPtr, const mwmp::Attack &attack)
{
    mwmp::BaseActor baseActor;
    baseActor.refNum = actorPtr.getCellRef().getRefNum().mIndex;
    baseActor.mpNum = actorPtr.getCellRef().getMpNum();
    baseActor.attack = attack;
    attackActors.push_back(baseActor);
}

void ActorList::addCastActor(BaseActor baseActor)
{
    castActors.push_back(baseActor);
}

void ActorList::addCellChangeActor(BaseActor baseActor)
{
    cellChangeActors.push_back(baseActor);
}

void ActorList::sendPositionActors()
{
    if (!setCell(cell))
        return;

    if (positionActors.size() > 0)
    {
        baseActors = positionActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_POSITION)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_POSITION)->Send();
    }
}

void ActorList::sendAnimFlagsActors()
{
    if (!setCell(cell))
        return;

    if (animFlagsActors.size() > 0)
    {
        baseActors = animFlagsActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_FLAGS)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_FLAGS)->Send();
    }
}

void ActorList::sendAnimPlayActors()
{
    if (!setCell(cell))
        return;

    if (animPlayActors.size() > 0)
    {
        baseActors = animPlayActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_PLAY)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_PLAY)->Send();
    }
}

void ActorList::sendSpeechActors()
{
    if (!setCell(cell))
        return;

    if (speechActors.size() > 0)
    {
        baseActors = speechActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPEECH)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPEECH)->Send();
    }
}

void ActorList::sendStatsDynamicActors()
{
    if (!setCell(cell))
        return;

    if (statsDynamicActors.size() > 0)
    {
        baseActors = statsDynamicActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->Send();
    }
}

void ActorList::sendDeathActors()
{
    if (!setCell(cell))
        return;

    if (deathActors.size() > 0)
    {
        baseActors = deathActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->Send();
    }
}

void ActorList::sendEquipmentActors()
{
    if (!setCell(cell))
        return;

    if (equipmentActors.size() > 0)
    {
        baseActors = equipmentActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->Send();
    }
}

void ActorList::sendAiActors()
{
    if (!setCell(cell))
        return;

    if (aiActors.size() > 0)
    {
        baseActors = aiActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_AI)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_AI)->Send();
    }
}

void ActorList::sendAttackActors()
{
    if (!setCell(cell))
        return;

    if (attackActors.size() > 0)
    {
        baseActors = attackActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ATTACK)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ATTACK)->Send();
    }
}

void ActorList::sendCastActors()
{
    if (!setCell(cell))
        return;

    if (castActors.size() > 0)
    {
        baseActors = castActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CAST)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CAST)->Send();
    }
}

void ActorList::sendCellChangeActors()
{
    if (!setCell(cell))
        return;

    if (cellChangeActors.size() > 0)
    {
        baseActors = cellChangeActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CELL_CHANGE)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CELL_CHANGE)->Send();
    }
}

void ActorList::sendActorsInCell(MWWorld::CellStore* cellStore)
{
    reset();
    if (!setCell(cellStore->getCell()->getEsm3()))
        return;
    action = BaseActorList::SET;

    for (auto &ref : cellStore->getNpcs()->mList)
    {
        MWWorld::Ptr ptr(&ref, 0);

        // If this Ptr is lacking a unique index, ignore it
        if (ptr.getCellRef().getRefNum().mIndex == 0 && ptr.getCellRef().getMpNum() == 0) continue;

        BaseActor actor;
        actor.refId = ptr.getCellRef().getRefId().getRefIdString();
        actor.refNum = ptr.getCellRef().getRefNum().mIndex;
        actor.mpNum = ptr.getCellRef().getMpNum();

        addActor(actor);
    }

    for (auto &ref : cellStore->getCreatures()->mList)
    {
        MWWorld::Ptr ptr(&ref, 0);

        // If this Ptr is lacking a unique index, ignore it
        if (ptr.getCellRef().getRefNum().mIndex == 0 && ptr.getCellRef().getMpNum() == 0) continue;

        BaseActor actor;
        actor.refId = ptr.getCellRef().getRefId().getRefIdString();
        actor.refNum = ptr.getCellRef().getRefNum().mIndex;
        actor.mpNum = ptr.getCellRef().getMpNum();

        addActor(actor);
    }

    mwmp::Main::get().getNetworking()->getActorPacket(ID_ACTOR_LIST)->setActorList(this);
    mwmp::Main::get().getNetworking()->getActorPacket(ID_ACTOR_LIST)->Send();
}
