#include "Shapeshift.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Networking.hpp>

#include <iostream>
#include <stdexcept>

double ShapeshiftFunctions::GetScale(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->scale;
}

bool ShapeshiftFunctions::IsWerewolf(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->isWerewolf;
}

const char *ShapeshiftFunctions::GetCreatureRefId(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->creatureRefId.c_str();
}

bool ShapeshiftFunctions::GetCreatureNameDisplayState(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->displayCreatureName;
}

void ShapeshiftFunctions::SetScale(unsigned short pid, double scale)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->scale = scale;
}

void ShapeshiftFunctions::SetWerewolfState(unsigned short pid, bool isWerewolf)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->isWerewolf = isWerewolf;
}

void ShapeshiftFunctions::SetCreatureRefId(unsigned short pid, const char *refId)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->creatureRefId = refId;
}

void ShapeshiftFunctions::SetCreatureNameDisplayState(unsigned short pid, bool displayState)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->displayCreatureName = displayState;
}

void ShapeshiftFunctions::SendShapeshift(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (mwmp::Networking::getPtr()->isPlayerShapeshiftIntentPending(*player))
        throw std::runtime_error(
            "a pending shapeshift intent cannot be sent before canonical commit");
    if (!mwmp::Networking::getPtr()->applyServerPlayerShapeshift(*player))
        throw std::runtime_error("the server-authored shapeshift state was rejected");

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_SHAPESHIFT);
    packet->setPlayer(player);

    packet->Send(false);
    packet->Send(true);
}
