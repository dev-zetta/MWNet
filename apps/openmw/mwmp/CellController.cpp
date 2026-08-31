#include <set>
#include <components/detournavigator/navigator.hpp>
#include <components/esm3/cellid.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>

#include "../mwbase/environment.hpp"

#include "../mwworld/containerstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/worldimp.hpp"

#include "CellController.hpp"
#include "Main.hpp"
#include "LocalActor.hpp"
#include "LocalPlayer.hpp"
using namespace mwmp;

std::map<std::string, std::unique_ptr<mwmp::Cell>> CellController::cellsInitialized;
std::map<std::string, std::string> CellController::localActorsToCells;
std::map<std::string, std::string> CellController::dedicatedActorsToCells;
std::map<std::string, unsigned int> CellController::queuedDeathStates;

mwmp::CellController::CellController()
{

}

CellController::~CellController()
{

}

void CellController::updateLocal(bool forceUpdate)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();

    // Loop through Cells, deleting inactive ones and updating LocalActors in active ones
    for (auto it = cellsInitialized.begin(); it != cellsInitialized.end();)
    {
        mwmp::Cell *mpCell = it->second.get();

        if (mpCell->getCellStore() == nullptr || mpCell->getCellStore()->getCell() == nullptr || !world->isCellActive(mpCell->getCellStore()->getCell()->getEsm3()))
        {
            mpCell->uninitializeLocalActors();
            mpCell->uninitializeDedicatedActors();
            it = cellsInitialized.erase(it);
        }
        else
        {
            mpCell->updateLocal(forceUpdate);
            ++it;
        }
    }

    // If there are cellsInitialized remaining, loop through them and initialize new LocalActors for eligible ones
    // 
    //
    // Note: This cannot be combined with the above loop because initializing LocalActors in a Cell before they are
    //       deleted from their previous one can make their records stay deleted
    if (cellsInitialized.size() > 0)
    {
        for (auto& cell : cellsInitialized)
        {
            mwmp::Cell* mpCell = cell.second.get();
            if (mpCell->shouldInitializeActors == true)
            {
                mpCell->shouldInitializeActors = false;
                mpCell->initializeLocalActors();
            }
        }
    }
    // Otherwise, disable the DetourNavigator for advanced pathfinding for the time being
    else
    {
        // setUpdatesEnabled removed in newer API
    }
}

void CellController::updateDedicated(float dt)
{
    for (const auto &cell : cellsInitialized)
        cell.second->updateDedicated(dt);
}

void CellController::initializeCell(const ESM::Cell& cell)
{
    std::string mapIndex = cell.getShortDescription();

    // If this key doesn't exist, create it
    if (cellsInitialized.count(mapIndex) == 0)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Initializing mwmp::Cell %s", cell.getShortDescription().c_str());

        MWWorld::CellStore *cellStore = getCellStore(cell);

        if (!cellStore) return;

        auto mpCell = std::make_unique<mwmp::Cell>(cellStore);
        mpCell->setAuthority(Main::get().getLocalPlayer()->guid);
        mpCell->shouldInitializeActors = true;
        cellsInitialized.insert_or_assign(mapIndex, std::move(mpCell));

        LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized mwmp::Cell %s", cell.getShortDescription().c_str());
    }
}

void CellController::uninitializeCell(const ESM::Cell& cell)
{
    std::string mapIndex = cell.getShortDescription();

    // If this key exists, erase the key-value pair from the map
    if (cellsInitialized.count(mapIndex) > 0)
    {
        mwmp::Cell* mpCell = cellsInitialized.at(mapIndex).get();
        mpCell->uninitializeLocalActors();
        mpCell->uninitializeDedicatedActors();
        cellsInitialized.erase(mapIndex);
    }
}

void CellController::uninitializeCells()
{
    if (cellsInitialized.size() > 0)
    {
        for (auto it = cellsInitialized.cbegin(); it != cellsInitialized.cend(); it++)
        {
            mwmp::Cell* mpCell = it->second.get();
            mpCell->uninitializeLocalActors();
            mpCell->uninitializeDedicatedActors();
        }

        cellsInitialized.clear();
    }
}

void CellController::readPositions(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readPositions(actorList);
}

void CellController::readAnimFlags(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readAnimFlags(actorList);
}

void CellController::readAnimPlay(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readAnimPlay(actorList);
}

void CellController::readStatsDynamic(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readStatsDynamic(actorList);
}

void CellController::readDeath(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readDeath(actorList);
}

void CellController::readEquipment(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readEquipment(actorList);
}

void CellController::readSpeech(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readSpeech(actorList);
}

void CellController::readSpellsActive(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readSpellsActive(actorList);
}

void CellController::readAi(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readAi(actorList);
}

void CellController::readAttack(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readAttack(actorList);
}

void CellController::readCast(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readCast(actorList);
}

void CellController::readCellChange(ActorList& actorList)
{
    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (Cell* cell = getCell(actorList.cell))
        cell->readCellChange(actorList);
}

bool CellController::hasQueuedDeathState(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);

    return queuedDeathStates.count(actorIndex) > 0;
}

unsigned int CellController::getQueuedDeathState(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);

    return queuedDeathStates[actorIndex];
}

void CellController::clearQueuedDeathState(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);

    queuedDeathStates.erase(actorIndex);
}

void CellController::setQueuedDeathState(MWWorld::Ptr ptr, unsigned int deathState)
{
    std::string actorIndex = generateMapIndex(ptr);

    queuedDeathStates[actorIndex] = deathState;
}

void CellController::setLocalActorRecord(std::string actorIndex, std::string cellIndex)
{
    localActorsToCells[actorIndex] = cellIndex;
}

void CellController::removeLocalActorRecord(std::string actorIndex)
{
    localActorsToCells.erase(actorIndex);
}

bool CellController::isLocalActor(MWWorld::Ptr ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    std::string actorIndex = generateMapIndex(ptr);
    return localActorsToCells.count(actorIndex) > 0;
}

bool CellController::isLocalActor(int refNum, int mpNum)
{
    std::string actorIndex = generateMapIndex(refNum, mpNum);
    return localActorsToCells.count(actorIndex) > 0;
}

LocalActor *CellController::getLocalActor(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);
    auto cellIt = localActorsToCells.find(actorIndex);
    if (cellIt == localActorsToCells.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getLocalActor: actor %s not in localActorsToCells", actorIndex.c_str());
        return nullptr;
    }
    auto cellIt2 = cellsInitialized.find(cellIt->second);
    if (cellIt2 == cellsInitialized.end())
    {
        static std::set<std::string> warned;
        if (warned.insert(actorIndex).second)
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getLocalActor: cell %s not in cellsInitialized for actor %s", cellIt->second.c_str(), actorIndex.c_str());
        return nullptr;
    }
    return cellIt2->second->getLocalActor(actorIndex);
}

LocalActor *CellController::getLocalActor(int refNum, int mpNum)
{
    std::string actorIndex = generateMapIndex(refNum, mpNum);
    auto cellIt = localActorsToCells.find(actorIndex);
    if (cellIt == localActorsToCells.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getLocalActor: actor %s not in localActorsToCells", actorIndex.c_str());
        return nullptr;
    }
    auto cellIt2 = cellsInitialized.find(cellIt->second);
    if (cellIt2 == cellsInitialized.end())
    {
        static std::set<std::string> warned;
        if (warned.insert(actorIndex).second)
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getLocalActor: cell %s not in cellsInitialized for actor %s", cellIt->second.c_str(), actorIndex.c_str());
        return nullptr;
    }
    return cellIt2->second->getLocalActor(actorIndex);
}

void CellController::setDedicatedActorRecord(std::string actorIndex, std::string cellIndex)
{
    dedicatedActorsToCells[actorIndex] = cellIndex;
}

void CellController::removeDedicatedActorRecord(std::string actorIndex)
{
    dedicatedActorsToCells.erase(actorIndex);
}

bool CellController::isDedicatedActor(MWWorld::Ptr ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    std::string actorIndex = generateMapIndex(ptr);
    return dedicatedActorsToCells.count(actorIndex) > 0;
}

bool CellController::isDedicatedActor(int refNum, int mpNum)
{
    std::string actorIndex = generateMapIndex(refNum, mpNum);
    return dedicatedActorsToCells.count(actorIndex) > 0;
}

DedicatedActor *CellController::getDedicatedActor(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);
    auto cellIt = dedicatedActorsToCells.find(actorIndex);
    if (cellIt == dedicatedActorsToCells.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getDedicatedActor: actor %s not in dedicatedActorsToCells", actorIndex.c_str());
        return nullptr;
    }
    auto cellIt2 = cellsInitialized.find(cellIt->second);
    if (cellIt2 == cellsInitialized.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getDedicatedActor: cell %s not in cellsInitialized for actor %s", cellIt->second.c_str(), actorIndex.c_str());
        return nullptr;
    }
    return cellIt2->second->getDedicatedActor(actorIndex);
}

DedicatedActor *CellController::getDedicatedActor(int refNum, int mpNum)
{
    std::string actorIndex = generateMapIndex(refNum, mpNum);
    auto cellIt = dedicatedActorsToCells.find(actorIndex);
    if (cellIt == dedicatedActorsToCells.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getDedicatedActor: actor %s not in dedicatedActorsToCells", actorIndex.c_str());
        return nullptr;
    }
    auto cellIt2 = cellsInitialized.find(cellIt->second);
    if (cellIt2 == cellsInitialized.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "getDedicatedActor: cell %s not in cellsInitialized for actor %s", cellIt->second.c_str(), actorIndex.c_str());
        return nullptr;
    }
    return cellIt2->second->getDedicatedActor(actorIndex);
}

std::string CellController::generateMapIndex(int refNum, int mpNum)
{
    std::string mapIndex = "";
    mapIndex = Utils::toString(refNum) + "-" + Utils::toString(mpNum);
    return mapIndex;
}

std::string CellController::generateMapIndex(MWWorld::Ptr ptr)
{
    return generateMapIndex(ptr.getCellRef().getRefNum().mIndex, ptr.getCellRef().getMpNum());
}

std::string CellController::generateMapIndex(BaseActor baseActor)
{
    return generateMapIndex(baseActor.refNum, baseActor.mpNum);
}

bool CellController::hasLocalAuthority(const ESM::Cell& cell)
{
    if (isInitializedCell(cell) && isActiveWorldCell(cell))
        return getCell(cell)->hasLocalAuthority();

    return false;
}

bool CellController::isInitializedCell(const std::string& cellDescription)
{
    return (cellsInitialized.count(cellDescription) > 0);
}

bool CellController::isInitializedCell(const ESM::Cell& cell)
{
    return isInitializedCell(cell.getShortDescription());
}

bool CellController::isActiveWorldCell(const ESM::Cell& cell)
{
    return MWBase::Environment::get().getWorld()->isCellActive(cell);
}

Cell *CellController::getCell(const ESM::Cell& cell)
{
    auto it = cellsInitialized.find(cell.getShortDescription());
    if (it == cellsInitialized.end())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "CellController::getCell: cell %s not in cellsInitialized", cell.getShortDescription().c_str());
        return nullptr;
    }
    return it->second.get();
}

MWWorld::CellStore *CellController::getCellStore(const ESM::Cell& cell)
{
    MWWorld::CellStore *cellStore;

    if (cell.isExterior())
        cellStore = &MWBase::Environment::get().getWorldModel()->getExterior(ESM::ExteriorCellLocation(cell.mData.mX, cell.mData.mY, ESM::Cell::sDefaultWorldspaceId));
    else
    {
        try
        {
            cellStore = &MWBase::Environment::get().getWorldModel()->getInterior(cell.mName);
        }
        catch (std::exception&)
        {
            cellStore = nullptr;
        }
    }

    return cellStore;
}

bool CellController::isSameCell(const ESM::Cell& cell, const ESM::Cell& otherCell)
{
    if (&cell == nullptr || &otherCell == nullptr) return false;

    bool isCellExterior = false;
    bool isOtherCellExterior = false;

    try
    {
        isCellExterior = cell.isExterior();
        isOtherCellExterior = otherCell.isExterior();
    }
    catch (std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed cell comparison");
        return false;
    }

    if (isCellExterior && isOtherCellExterior)
    {
        if (cell.mData.mX == otherCell.mData.mX && cell.mData.mY == otherCell.mData.mY)
            return true;
    }
    else if (Misc::StringUtils::ciEqual(cell.mName, otherCell.mName))
        return true;

    return false;
}

int CellController::getCellSize() const
{
    return 8192;
}
