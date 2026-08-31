#include <components/openmw-mp/NetworkMessages.hpp>

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/CellController.hpp>
#include <cstddef>
#include <fstream>
#include <span>
#include <stdexcept>

#include <apps/openmw-mp/Utils.hpp>

#include "Worldstate.hpp"

using namespace mwmp;

BaseWorldstate *WorldstateFunctions::readWorldstate;
BaseWorldstate WorldstateFunctions::writeWorldstate;

void WorldstateFunctions::ReadReceivedWorldstate()
{
    readWorldstate = mwmp::Networking::getPtr()->getReceivedWorldstate();
}

void WorldstateFunctions::CopyReceivedWorldstateToStore()
{
    writeWorldstate = *readWorldstate;
}

void WorldstateFunctions::ClearKillChanges()
{
    writeWorldstate.killChanges.clear();
}

void WorldstateFunctions::ClearMapChanges()
{
    writeWorldstate.mapTiles.clear();
}

void WorldstateFunctions::ClearClientGlobals()
{
    writeWorldstate.clientGlobals.clear();
}

unsigned int WorldstateFunctions::GetKillChangesSize()
{
    return readWorldstate->killChanges.size();
}

unsigned int WorldstateFunctions::GetMapChangesSize()
{
    return readWorldstate->mapTiles.size();
}

unsigned int WorldstateFunctions::GetClientGlobalsSize()
{
    return readWorldstate->clientGlobals.size();
}

const char *WorldstateFunctions::GetKillRefId(unsigned int index)
{
    return readWorldstate->killChanges.at(index).refId.c_str();
}

int WorldstateFunctions::GetKillNumber(unsigned int index)
{
    return readWorldstate->killChanges.at(index).number;
}

const char *WorldstateFunctions::GetWeatherRegion()
{
    return readWorldstate->weather.region.c_str();
}

int WorldstateFunctions::GetWeatherCurrent()
{
    return readWorldstate->weather.currentWeather;
}

int WorldstateFunctions::GetWeatherNext()
{
    return readWorldstate->weather.nextWeather;
}

int WorldstateFunctions::GetWeatherQueued()
{
    return readWorldstate->weather.queuedWeather;
}

double WorldstateFunctions::GetWeatherTransitionFactor()
{
    return readWorldstate->weather.transitionFactor;
}

int WorldstateFunctions::GetMapTileCellX(unsigned int index)
{
    return readWorldstate->mapTiles.at(index).x;
}

int WorldstateFunctions::GetMapTileCellY(unsigned int index)
{
    return readWorldstate->mapTiles.at(index).y;
}

const char *WorldstateFunctions::GetClientGlobalId(unsigned int index)
{
    return readWorldstate->clientGlobals.at(index).id.c_str();
}

unsigned short WorldstateFunctions::GetClientGlobalVariableType(unsigned int index)
{
    return readWorldstate->clientGlobals.at(index).variableType;
}

int WorldstateFunctions::GetClientGlobalIntValue(unsigned int index)
{
    return readWorldstate->clientGlobals.at(index).intValue;
}

double WorldstateFunctions::GetClientGlobalFloatValue(unsigned int index)
{
    return readWorldstate->clientGlobals.at(index).floatValue;
}

void WorldstateFunctions::SetAuthorityRegion(const char* authorityRegion)
{
    writeWorldstate.authorityRegion = authorityRegion;
}

void WorldstateFunctions::SetWeatherRegion(const char* region)
{
    writeWorldstate.weather.region = region;
}

void WorldstateFunctions::SetWeatherForceState(bool forceState)
{
    writeWorldstate.forceWeather = forceState;
}

void WorldstateFunctions::SetWeatherCurrent(int currentWeather)
{
    writeWorldstate.weather.currentWeather = currentWeather;
}

void WorldstateFunctions::SetWeatherNext(int nextWeather)
{
    writeWorldstate.weather.nextWeather = nextWeather;
}

void WorldstateFunctions::SetWeatherQueued(int queuedWeather)
{
    writeWorldstate.weather.queuedWeather = queuedWeather;
}

void WorldstateFunctions::SetWeatherTransitionFactor(double transitionFactor)
{
    writeWorldstate.weather.transitionFactor = transitionFactor;
}

void WorldstateFunctions::SetHour(double hour)
{
    writeWorldstate.time.hour = hour;
}

void WorldstateFunctions::SetDay(int day)
{
    writeWorldstate.time.day = day;
}

void WorldstateFunctions::SetMonth(int month)
{
    writeWorldstate.time.month = month;
}

void WorldstateFunctions::SetYear(int year)
{
    writeWorldstate.time.year = year;
}

void WorldstateFunctions::SetDaysPassed(int daysPassed)
{
    writeWorldstate.time.daysPassed = daysPassed;
}

void WorldstateFunctions::SetTimeScale(double timeScale)
{
    writeWorldstate.time.timeScale = timeScale;
}

void WorldstateFunctions::SetPlayerCollisionState(bool state)
{
    writeWorldstate.hasPlayerCollision = state;
}

void WorldstateFunctions::SetActorCollisionState(bool state)
{
    writeWorldstate.hasActorCollision = state;
}

void WorldstateFunctions::SetPlacedObjectCollisionState(bool state)
{
    writeWorldstate.hasPlacedObjectCollision = state;
}

void WorldstateFunctions::UseActorCollisionForPlacedObjects(bool useActorCollision)
{
    writeWorldstate.useActorCollisionForPlacedObjects = useActorCollision;
}

void WorldstateFunctions::AddKill(const char* refId, int number)
{
    mwmp::Kill kill;
    kill.refId = refId;
    kill.number = number;

    writeWorldstate.killChanges.push_back(kill);
}

void WorldstateFunctions::AddClientGlobalInteger(const char* id, int intValue, unsigned int variableType)
{
    mwmp::ClientVariable clientVariable;
    clientVariable.id = id;
    clientVariable.variableType = variableType;
    clientVariable.intValue = intValue;

    writeWorldstate.clientGlobals.push_back(clientVariable);
}

void WorldstateFunctions::AddClientGlobalFloat(const char* id, double floatValue)
{
    mwmp::ClientVariable clientVariable;
    clientVariable.id = id;
    clientVariable.variableType = mwmp::VARIABLE_TYPE::FLOAT;
    clientVariable.floatValue = floatValue;

    writeWorldstate.clientGlobals.push_back(clientVariable);
}

void WorldstateFunctions::AddSynchronizedClientScriptId(const char *scriptId)
{
    writeWorldstate.synchronizedClientScriptIds.push_back(scriptId);
}

void WorldstateFunctions::AddSynchronizedClientGlobalId(const char *globalId)
{
    writeWorldstate.synchronizedClientGlobalIds.push_back(globalId);
}

void WorldstateFunctions::AddEnforcedCollisionRefId(const char *refId)
{
    writeWorldstate.enforcedCollisionRefIds.push_back(refId);
}

void WorldstateFunctions::AddCellToReset(const char *cellDescription)
{
    ESM::Cell cell = Utils::getCellFromDescription(cellDescription);
    writeWorldstate.cellsToReset.push_back(cell);
}

void WorldstateFunctions::AddDestinationOverride(const char *oldCellDescription, const char *newCellDescription)
{
    writeWorldstate.destinationOverrides[oldCellDescription] = newCellDescription;
}

void WorldstateFunctions::ClearSynchronizedClientScriptIds()
{
    writeWorldstate.synchronizedClientScriptIds.clear();
}

void WorldstateFunctions::ClearSynchronizedClientGlobalIds()
{
    writeWorldstate.synchronizedClientGlobalIds.clear();
}

void WorldstateFunctions::ClearEnforcedCollisionRefIds()
{
    writeWorldstate.enforcedCollisionRefIds.clear();
}

void WorldstateFunctions::ClearCellsToReset()
{
    writeWorldstate.cellsToReset.clear();
}

void WorldstateFunctions::ClearDestinationOverrides()
{
    writeWorldstate.destinationOverrides.clear();
}

void WorldstateFunctions::SaveMapTileImageFile(unsigned int index, const char *filePath)
{
    if (readWorldstate == nullptr || index >= readWorldstate->mapTiles.size())
        throw std::out_of_range("map tile index is outside the received worldstate");
    if (filePath == nullptr || *filePath == '\0')
        throw std::invalid_argument("map tile output path is empty");

    const std::vector<char>& imageData = readWorldstate->mapTiles.at(index).imageData;
    if (imageData.size() > static_cast<std::size_t>(mwmp::maxImageDataSize))
        throw std::length_error("map tile image exceeds the 1,800-byte limit");

    mwmp::persistence::AtomicWriteOptions options;
    options.backup = mwmp::persistence::BackupPolicy::MaintainOne;
    options.maximumBytes = mwmp::maxImageDataSize;
    const auto bytes = std::as_bytes(std::span(imageData));
    const auto decision = mwmp::Networking::getPtr()->queuePersistenceWrite(
        filePath, bytes, std::move(options));
    if (decision != mwmp::persistence::QueueDecision::Queued
        && decision != mwmp::persistence::QueueDecision::Coalesced)
    {
        throw std::runtime_error(std::string("map tile persistence was rejected: ")
            + mwmp::persistence::describe(decision));
    }
}

void WorldstateFunctions::LoadMapTileImageFile(int cellX, int cellY, const char* filePath)
{
    if (filePath == nullptr || *filePath == '\0')
        throw std::invalid_argument("map tile input path is empty");

    mwmp::MapTile mapTile;
    mapTile.x = cellX;
    mapTile.y = cellY;

    std::ifstream inputFile(filePath, std::ios::binary | std::ios::ate);
    if (!inputFile)
        throw std::runtime_error("failed to open map tile image");
    const std::streampos size = inputFile.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > mwmp::maxImageDataSize)
        throw std::length_error("map tile image exceeds the 1,800-byte limit");
    mapTile.imageData.resize(static_cast<std::size_t>(size));
    inputFile.seekg(0);
    if (!mapTile.imageData.empty())
        inputFile.read(mapTile.imageData.data(), size);
    if (!inputFile)
        throw std::runtime_error("failed to read map tile image");
    writeWorldstate.mapTiles.push_back(std::move(mapTile));
}

void WorldstateFunctions::SendClientScriptGlobal(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_CLIENT_SCRIPT_GLOBAL);
    packet->setWorldstate(&writeWorldstate);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendClientScriptSettings(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_CLIENT_SCRIPT_SETTINGS);
    packet->setWorldstate(&writeWorldstate);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendWorldKillCount(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_WORLD_KILL_COUNT);
    packet->setWorldstate(&writeWorldstate);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendWorldMap(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_WORLD_MAP);
    packet->setWorldstate(&writeWorldstate);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendWorldTime(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_WORLD_TIME);
    packet->setWorldstate(&writeWorldstate);
    
    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendWorldWeather(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_WORLD_WEATHER);
    packet->setWorldstate(&writeWorldstate);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendWorldCollisionOverride(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_WORLD_COLLISION_OVERRIDE);
    packet->setWorldstate(&writeWorldstate);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendCellReset(unsigned short pid, bool sendToOtherPlayers)
{
    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_CELL_RESET);

    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    packet->setWorldstate(&writeWorldstate);

    packet->Send(sendToOtherPlayers);

    if (sendToOtherPlayers)
    {
        packet->Send(false);
    }
}

void WorldstateFunctions::SendWorldDestinationOverride(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_WORLD_DESTINATION_OVERRIDE);
    packet->setWorldstate(&writeWorldstate);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void WorldstateFunctions::SendWorldRegionAuthority(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeWorldstate.guid = player->guid;

    mwmp::WorldstatePacket *packet = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_WORLD_REGION_AUTHORITY);
    packet->setWorldstate(&writeWorldstate);

    packet->Send(false);

    // This packet should always be sent to all other players
    packet->Send(true);
}

// All methods below are deprecated versions of methods from above

void WorldstateFunctions::ReadLastWorldstate()
{
    ReadReceivedWorldstate();
}

void WorldstateFunctions::CopyLastWorldstateToStore()
{
    CopyReceivedWorldstateToStore();
}
