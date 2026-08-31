#include "Cells.hpp"

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Networking.hpp>

#include <iostream>

static std::string tempCellDescription;

unsigned int CellFunctions::GetCellStateChangesSize(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->cellStateChanges.size();
}

unsigned int CellFunctions::GetCellStateType(unsigned short pid, unsigned int index)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->cellStateChanges.at(index).type;
}

const char *CellFunctions::GetCellStateDescription(unsigned short pid, unsigned int index)
{
    Player *player;
    GET_PLAYER(pid, player, "");

    if (index >= player->cellStateChanges.size())
        return "invalid";

    tempCellDescription = player->cellStateChanges.at(index).cell.getShortDescription();
    return tempCellDescription.c_str();
}

const char *CellFunctions::GetCell(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    tempCellDescription = player->cell.getShortDescription().c_str();
    return tempCellDescription.c_str();
}

int CellFunctions::GetExteriorX(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);
    return player->cell.mData.mX;
}

int CellFunctions::GetExteriorY(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);
    return player->cell.mData.mY;
}

bool CellFunctions::IsInExterior(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->cell.isExterior();
}

const char *CellFunctions::GetRegion(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->cell.mRegion.getRefIdString().c_str();
}

bool CellFunctions::IsChangingRegion(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->isChangingRegion;
}

void CellFunctions::SetCell(unsigned short pid, const char *cellDescription)
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Script is moving %s from %s to %s", player->npc.mName.c_str(),
                       player->cell.getShortDescription().c_str(), cellDescription);

    player->cell = Utils::getCellFromDescription(cellDescription);
}

void CellFunctions::SetExteriorCell(unsigned short pid, int x, int y)
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Script is moving %s from %s to %i,%i", player->npc.mName.c_str(),
                       player->cell.getShortDescription().c_str(), x, y);

    // If the player is currently in an interior, turn off the interior flag
    // from the cell
    if (!player->cell.isExterior())
        player->cell.mData.mFlags &= ~ESM::Cell::Interior;

    player->cell.mData.mX = x;
    player->cell.mData.mY = y;
}

void CellFunctions::SendCell(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_CELL_CHANGE);
    packet->setPlayer(player);

    mwmp::Networking::getPtr()->authorizePlayerMovement(*player);

    packet->Send(false);
}
