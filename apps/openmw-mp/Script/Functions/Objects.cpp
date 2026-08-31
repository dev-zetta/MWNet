#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Utils.hpp>
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>

#include <stdexcept>

#include "Objects.hpp"

using namespace mwmp;

BaseObjectList *readObjectList;
BaseObjectList writeObjectList;

BaseObject tempObject;
const BaseObject emptyObject = {};

ContainerItem tempContainerItem;
const ContainerItem emptyContainerItem = {};

namespace
{
    BaseObjectList& requireReadObjectList()
    {
        if (readObjectList == nullptr)
        {
            throw std::runtime_error(
                "no object list is selected; call ReadReceivedObjectList first");
        }
        return *readObjectList;
    }
}

void ObjectFunctions::ReadReceivedObjectList()
{
    readObjectList = mwmp::Networking::getPtr()->getReceivedObjectList();
}

void ObjectFunctions::ClearObjectList()
{
    writeObjectList.cell.blank();
    writeObjectList.baseObjects.clear();
    writeObjectList.packetOrigin = mwmp::PACKET_ORIGIN::SERVER_SCRIPT;
}

void ObjectFunctions::SetObjectListPid(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeObjectList.guid = player->guid;
}

void ObjectFunctions::CopyReceivedObjectListToStore()
{
    writeObjectList = requireReadObjectList();
}

bool ObjectFunctions::SeedContainerInventory()
{
    return mwmp::Networking::getPtr()->seedServerContainerInventory(writeObjectList);
}

bool ObjectFunctions::SeedObjectState()
{
    return mwmp::Networking::getPtr()->seedServerObjectState(writeObjectList);
}

unsigned int ObjectFunctions::GetObjectListSize()
{
    return requireReadObjectList().baseObjectCount;
}

unsigned char ObjectFunctions::GetObjectListOrigin()
{
    return requireReadObjectList().packetOrigin;
}

const char *ObjectFunctions::GetObjectListClientScript()
{
    return requireReadObjectList().originClientScript.c_str();
}

unsigned char ObjectFunctions::GetObjectListAction()
{
    return requireReadObjectList().action;
}

const char *ObjectFunctions::GetObjectListConsoleCommand()
{
    return requireReadObjectList().consoleCommand.c_str();
}

unsigned char ObjectFunctions::GetObjectListContainerSubAction()
{
    return requireReadObjectList().containerSubAction;
}

bool ObjectFunctions::IsObjectPlayer(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).isPlayer;
}

int ObjectFunctions::GetObjectPid(unsigned int index)
{
    Player *player = Players::getPlayer(requireReadObjectList().baseObjects.at(index).guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectRefId(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).refId.c_str();
}

unsigned int ObjectFunctions::GetObjectRefNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).refNum;
}

unsigned int ObjectFunctions::GetObjectMpNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).mpNum;
}

int ObjectFunctions::GetObjectCount(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).count;
}

int ObjectFunctions::GetObjectCharge(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).charge;
}

double ObjectFunctions::GetObjectEnchantmentCharge(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).enchantmentCharge;
}

const char *ObjectFunctions::GetObjectSoul(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).soul.c_str();
}

int ObjectFunctions::GetObjectGoldValue(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).goldValue;
}

double ObjectFunctions::GetObjectScale(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).scale;
}

const char *ObjectFunctions::GetObjectSoundId(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).soundId.c_str();
}

bool ObjectFunctions::GetObjectState(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).objectState;
}

int ObjectFunctions::GetObjectDoorState(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).doorState;
}

int ObjectFunctions::GetObjectLockLevel(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).lockLevel;
}

unsigned int ObjectFunctions::GetObjectDialogueChoiceType(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).dialogueChoiceType;
}

const char* ObjectFunctions::GetObjectDialogueChoiceTopic(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).topicId.c_str();
}

unsigned int ObjectFunctions::GetObjectGoldPool(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).goldPool;
}

double ObjectFunctions::GetObjectLastGoldRestockHour(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).lastGoldRestockHour;
}

int ObjectFunctions::GetObjectLastGoldRestockDay(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).lastGoldRestockDay;
}

bool ObjectFunctions::DoesObjectHavePlayerActivating(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).activatingActor.isPlayer;
}

int ObjectFunctions::GetObjectActivatingPid(unsigned int index)
{
    Player *player = Players::getPlayer(requireReadObjectList().baseObjects.at(index).activatingActor.guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectActivatingRefId(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).activatingActor.refId.c_str();
}

unsigned int ObjectFunctions::GetObjectActivatingRefNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).activatingActor.refNum;
}

unsigned int ObjectFunctions::GetObjectActivatingMpNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).activatingActor.mpNum;
}

const char *ObjectFunctions::GetObjectActivatingName(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).activatingActor.name.c_str();
}

bool ObjectFunctions::GetObjectHitSuccess(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hitAttack.success;
}

double ObjectFunctions::GetObjectHitDamage(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hitAttack.damage;
}

bool ObjectFunctions::GetObjectHitBlock(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hitAttack.block;
}

bool ObjectFunctions::GetObjectHitKnockdown(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hitAttack.knockdown;
}

bool ObjectFunctions::DoesObjectHavePlayerHitting(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hittingActor.isPlayer;
}

int ObjectFunctions::GetObjectHittingPid(unsigned int index)
{
    Player *player = Players::getPlayer(requireReadObjectList().baseObjects.at(index).hittingActor.guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectHittingRefId(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hittingActor.refId.c_str();
}

unsigned int ObjectFunctions::GetObjectHittingRefNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hittingActor.refNum;
}

unsigned int ObjectFunctions::GetObjectHittingMpNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hittingActor.mpNum;
}

const char *ObjectFunctions::GetObjectHittingName(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hittingActor.name.c_str();
}

bool ObjectFunctions::GetObjectSummonState(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).isSummon;
}

double ObjectFunctions::GetObjectSummonDuration(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).summonDuration;
}

double ObjectFunctions::GetObjectSummonEffectId(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).summonEffectId;
}

const char *ObjectFunctions::GetObjectSummonSpellId(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).summonSpellId.c_str();
}

bool ObjectFunctions::DoesObjectHavePlayerSummoner(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).master.isPlayer;
}

int ObjectFunctions::GetObjectSummonerPid(unsigned int index)
{
    Player *player = Players::getPlayer(requireReadObjectList().baseObjects.at(index).master.guid);
    
    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectSummonerRefId(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).master.refId.c_str();
}

unsigned int ObjectFunctions::GetObjectSummonerRefNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).master.refNum;
}

unsigned int ObjectFunctions::GetObjectSummonerMpNum(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).master.mpNum;
}

double ObjectFunctions::GetObjectPosX(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).position.pos[0];
}

double ObjectFunctions::GetObjectPosY(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).position.pos[1];
}

double ObjectFunctions::GetObjectPosZ(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).position.pos[2];
}

double ObjectFunctions::GetObjectRotX(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).position.rot[0];
}

double ObjectFunctions::GetObjectRotY(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).position.rot[1];
}

double ObjectFunctions::GetObjectRotZ(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).position.rot[2];
}

const char *ObjectFunctions::GetVideoFilename(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).videoFilename.c_str();
}

unsigned int ObjectFunctions::GetClientLocalsSize(unsigned int objectIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex).clientLocals.size();
}

unsigned int ObjectFunctions::GetClientLocalInternalIndex(unsigned int objectIndex, unsigned int variableIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex).clientLocals.at(variableIndex).internalIndex;
}

unsigned short ObjectFunctions::GetClientLocalVariableType(unsigned int objectIndex, unsigned int variableIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex).clientLocals.at(variableIndex).variableType;
}

int ObjectFunctions::GetClientLocalIntValue(unsigned int objectIndex, unsigned int variableIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex).clientLocals.at(variableIndex).intValue;
}

double ObjectFunctions::GetClientLocalFloatValue(unsigned int objectIndex, unsigned int variableIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex).clientLocals.at(variableIndex).floatValue;
}

unsigned int ObjectFunctions::GetContainerChangesSize(unsigned int objectIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex).containerItemCount;
}

const char *ObjectFunctions::GetContainerItemRefId(unsigned int objectIndex, unsigned int itemIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex)
        .containerItems.at(itemIndex).refId.c_str();
}

int ObjectFunctions::GetContainerItemCount(unsigned int objectIndex, unsigned int itemIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex)
        .containerItems.at(itemIndex).count;
}

int ObjectFunctions::GetContainerItemCharge(unsigned int objectIndex, unsigned int itemIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex)
        .containerItems.at(itemIndex).charge;
}

double ObjectFunctions::GetContainerItemEnchantmentCharge(unsigned int objectIndex, unsigned int itemIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex)
        .containerItems.at(itemIndex).enchantmentCharge;
}

const char *ObjectFunctions::GetContainerItemSoul(unsigned int objectIndex, unsigned int itemIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex)
        .containerItems.at(itemIndex).soul.c_str();
}

int ObjectFunctions::GetContainerItemActionCount(unsigned int objectIndex, unsigned int itemIndex)
{
    return requireReadObjectList().baseObjects.at(objectIndex)
        .containerItems.at(itemIndex).actionCount;
}

bool ObjectFunctions::DoesObjectHaveContainer(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).hasContainer;
}

bool ObjectFunctions::IsObjectDroppedByPlayer(unsigned int index)
{
    return requireReadObjectList().baseObjects.at(index).droppedByPlayer;
}

void ObjectFunctions::SetObjectListCell(const char* cellDescription)
{
    writeObjectList.cell = Utils::getCellFromDescription(cellDescription);
}

void ObjectFunctions::SetObjectListAction(unsigned char action)
{
    writeObjectList.action = action;
}

void ObjectFunctions::SetObjectListContainerSubAction(unsigned char containerSubAction)
{
    writeObjectList.containerSubAction = containerSubAction;
}

void ObjectFunctions::SetObjectListConsoleCommand(const char* consoleCommand)
{
    writeObjectList.consoleCommand = consoleCommand;
}

void ObjectFunctions::SetObjectRefId(const char* refId)
{
    tempObject.refId = refId;
}

void ObjectFunctions::SetObjectRefNum(int refNum)
{
    tempObject.refNum = refNum;
}

void ObjectFunctions::SetObjectMpNum(int mpNum)
{
    tempObject.mpNum = mpNum;
}

void ObjectFunctions::SetObjectCount(int count)
{
    tempObject.count = count;
}

void ObjectFunctions::SetObjectCharge(int charge)
{
    tempObject.charge = charge;
}

void ObjectFunctions::SetObjectEnchantmentCharge(double enchantmentCharge)
{
    tempObject.enchantmentCharge = enchantmentCharge;
}

void ObjectFunctions::SetObjectSoul(const char* soul)
{
    tempObject.soul = soul;
}

void ObjectFunctions::SetObjectGoldValue(int goldValue)
{
    tempObject.goldValue = goldValue;
}

void ObjectFunctions::SetObjectScale(double scale)
{
    tempObject.scale = scale;
}

void ObjectFunctions::SetObjectState(bool objectState)
{
    tempObject.objectState = objectState;
}

void ObjectFunctions::SetObjectLockLevel(int lockLevel)
{
    tempObject.lockLevel = lockLevel;
}

void ObjectFunctions::SetObjectDialogueChoiceType(unsigned int dialogueChoiceType)
{
    tempObject.dialogueChoiceType = dialogueChoiceType;
}

void ObjectFunctions::SetObjectDialogueChoiceTopic(const char* topic)
{
    tempObject.topicId = topic;
}

void ObjectFunctions::SetObjectGoldPool(unsigned int goldPool)
{
    tempObject.goldPool = goldPool;
}

void ObjectFunctions::SetObjectLastGoldRestockHour(double lastGoldRestockHour)
{
    tempObject.lastGoldRestockHour = lastGoldRestockHour;
}

void ObjectFunctions::SetObjectLastGoldRestockDay(int lastGoldRestockDay)
{
    tempObject.lastGoldRestockDay = lastGoldRestockDay;
}

void ObjectFunctions::SetObjectDisarmState(bool disarmState)
{
    tempObject.isDisarmed = disarmState;
}

void ObjectFunctions::SetObjectDroppedByPlayerState(bool droppedByPlayer)
{
    tempObject.droppedByPlayer = droppedByPlayer;
}

void ObjectFunctions::SetObjectPosition(double x, double y, double z)
{
    tempObject.position.pos[0] = x;
    tempObject.position.pos[1] = y;
    tempObject.position.pos[2] = z;
}

void ObjectFunctions::SetObjectRotation(double x, double y, double z)
{
    tempObject.position.rot[0] = x;
    tempObject.position.rot[1] = y;
    tempObject.position.rot[2] = z;
}

void ObjectFunctions::SetObjectSound(const char* soundId, double volume, double pitch)
{
    tempObject.soundId = soundId;
    tempObject.volume = volume;
    tempObject.pitch = pitch;
}

void ObjectFunctions::SetObjectSummonState(bool summonState)
{
    tempObject.isSummon = summonState;
}

void ObjectFunctions::SetObjectSummonEffectId(int summonEffectId)
{
    tempObject.summonEffectId = summonEffectId;
}

void ObjectFunctions::SetObjectSummonSpellId(const char* summonSpellId)
{
    tempObject.summonSpellId = summonSpellId;
}

void ObjectFunctions::SetObjectSummonDuration(double summonDuration)
{
    tempObject.summonDuration = summonDuration;
}

void ObjectFunctions::SetObjectSummonerPid(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    tempObject.master.isPlayer = true;
    tempObject.master.guid = player->guid;
}

void ObjectFunctions::SetObjectSummonerRefNum(int refNum)
{
    tempObject.master.isPlayer = false;
    tempObject.master.refNum = refNum;
}

void ObjectFunctions::SetObjectSummonerMpNum(int mpNum)
{
    tempObject.master.isPlayer = false;
    tempObject.master.mpNum = mpNum;
}

void ObjectFunctions::SetObjectActivatingPid(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    tempObject.activatingActor.isPlayer = true;
    tempObject.activatingActor.guid = player->guid;
}

void ObjectFunctions::SetObjectDoorState(int doorState)
{
    tempObject.doorState = doorState;
}

void ObjectFunctions::SetObjectDoorTeleportState(bool teleportState)
{
    tempObject.teleportState = teleportState;
}

void ObjectFunctions::SetObjectDoorDestinationCell(const char* cellDescription)
{
    tempObject.destinationCell = Utils::getCellFromDescription(cellDescription);
}

void ObjectFunctions::SetObjectDoorDestinationPosition(double x, double y, double z)
{
    tempObject.destinationPosition.pos[0] = x;
    tempObject.destinationPosition.pos[1] = y;
    tempObject.destinationPosition.pos[2] = z;
}

void ObjectFunctions::SetObjectDoorDestinationRotation(double x, double z)
{
    tempObject.destinationPosition.rot[0] = x;
    tempObject.destinationPosition.rot[2] = z;
}

void ObjectFunctions::SetPlayerAsObject(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    tempObject.guid = player->guid;
    tempObject.isPlayer = true;
}

void ObjectFunctions::SetContainerItemRefId(const char* refId)
{
    tempContainerItem.refId = refId;
}

void ObjectFunctions::SetContainerItemCount(int count)
{
    tempContainerItem.count = count;
}

void ObjectFunctions::SetContainerItemCharge(int charge)
{
    tempContainerItem.charge = charge;
}

void ObjectFunctions::SetContainerItemEnchantmentCharge(double enchantmentCharge)
{
    tempContainerItem.enchantmentCharge = enchantmentCharge;
}

void ObjectFunctions::SetContainerItemSoul(const char* soul)
{
    tempContainerItem.soul = soul;
}

void ObjectFunctions::SetContainerItemActionCountByIndex(unsigned int objectIndex, unsigned int itemIndex, int actionCount)
{
    writeObjectList.baseObjects.at(objectIndex).containerItems.at(itemIndex).actionCount = actionCount;
}

void ObjectFunctions::AddObject()
{
    writeObjectList.baseObjects.push_back(tempObject);

    tempObject = emptyObject;
}

void ObjectFunctions::AddClientLocalInteger(int internalIndex, int intValue, unsigned int variableType)
{
    ClientVariable clientLocal;
    clientLocal.internalIndex = internalIndex;
    clientLocal.intValue = intValue;
    clientLocal.variableType = variableType;

    tempObject.clientLocals.push_back(clientLocal);
}

void ObjectFunctions::AddClientLocalFloat(int internalIndex, double floatValue)
{
    ClientVariable clientLocal;
    clientLocal.internalIndex = internalIndex;
    clientLocal.floatValue = floatValue;
    clientLocal.variableType = mwmp::VARIABLE_TYPE::FLOAT;

    tempObject.clientLocals.push_back(clientLocal);
}

void ObjectFunctions::AddContainerItem()
{
    tempObject.containerItems.push_back(tempContainerItem);

    tempContainerItem = emptyContainerItem;
}

void ObjectFunctions::SendObjectActivate(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_ACTIVATE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectPlace(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_PLACE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectSpawn(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_SPAWN);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectDelete(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_DELETE);
    packet->setObjectList(&writeObjectList);
    
    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectLock(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_LOCK);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectDialogueChoice(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket* packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_DIALOGUE_CHOICE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectMiscellaneous(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket* packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_MISCELLANEOUS);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectRestock(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_RESTOCK);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectTrap(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_TRAP);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectScale(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_SCALE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectSound(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_SOUND);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectState(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_STATE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectMove(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket* packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_MOVE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendObjectRotate(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket* packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_OBJECT_ROTATE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendDoorState(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_DOOR_STATE);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendDoorDestination(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_DOOR_DESTINATION);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendContainer(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_CONTAINER);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendVideoPlay(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_VIDEO_PLAY);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendClientScriptLocal(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket* packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_CLIENT_SCRIPT_LOCAL);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ObjectFunctions::SendConsoleCommand(bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    mwmp::ObjectPacket *packet = mwmp::Networking::get().getObjectPacketController()->GetPacket(ID_CONSOLE_COMMAND);
    packet->setObjectList(&writeObjectList);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}


// All methods below are deprecated versions of methods from above

void ObjectFunctions::ReadLastObjectList()
{
    ReadReceivedObjectList();
}

void ObjectFunctions::ReadLastEvent()
{
    ReadReceivedObjectList();
}

void ObjectFunctions::InitializeObjectList(unsigned short pid)
{
    ClearObjectList();
    SetObjectListPid(pid);
}

void ObjectFunctions::InitializeEvent(unsigned short pid)
{
    InitializeObjectList(pid);
}

void ObjectFunctions::CopyLastObjectListToStore()
{
    CopyReceivedObjectListToStore();
}

unsigned int ObjectFunctions::GetObjectChangesSize()
{
    return GetObjectListSize();
}

unsigned char ObjectFunctions::GetEventAction()
{
    return GetObjectListAction();
}

unsigned char ObjectFunctions::GetEventContainerSubAction()
{
    return GetObjectListContainerSubAction();
}

unsigned int ObjectFunctions::GetObjectRefNumIndex(unsigned int index)
{
    return GetObjectRefNum(index);
}

unsigned int ObjectFunctions::GetObjectSummonerRefNumIndex(unsigned int index)
{
    return GetObjectSummonerRefNum(index);
}

void ObjectFunctions::SetEventCell(const char* cellDescription)
{
    SetObjectListCell(cellDescription);
}

void ObjectFunctions::SetEventAction(unsigned char action)
{
    SetObjectListAction(action);
}

void ObjectFunctions::SetEventConsoleCommand(const char* consoleCommand)
{
    SetObjectListConsoleCommand(consoleCommand);
}

void ObjectFunctions::SetObjectRefNumIndex(int refNum)
{
    SetObjectRefNum(refNum);
}

void ObjectFunctions::AddWorldObject()
{
    AddObject();
}
