#include "Player.hpp"

#include <algorithm>
#include <limits>

TPlayers Players::players;
TSlots Players::slots;

bool Players::deletePlayer(mwmp::transport::TransportConnectionId guid)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Deleting player connection %llu",
        static_cast<unsigned long long>(guid.value));

    const auto playerIt = players.find(guid);
    if (playerIt == players.end() || !playerIt->second)
        return false;

    Player* player = playerIt->second.get();
    CellController::get()->deletePlayer(player);

    LOG_APPEND(TimedLog::LOG_INFO, "- Emptying slot %i", player->getId());

    const auto slotIt = slots.find(player->getId());
    if (slotIt != slots.end() && slotIt->second == player)
        slots.erase(slotIt);
    players.erase(playerIt);
    return true;
}

Players::CreationResult Players::newPlayer(mwmp::transport::TransportConnectionId guid, unsigned int maximumPlayers)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Creating player connection %llu",
        static_cast<unsigned long long>(guid.value));

    const auto existingPlayer = players.find(guid);
    if (existingPlayer != players.end())
        return { existingPlayer->second.get(), CreationStatus::AlreadyExists };

    const unsigned int slotLimit = std::min(maximumPlayers,
        static_cast<unsigned int>(std::numeric_limits<unsigned short>::max()) + 1u);
    unsigned int selectedSlot = slotLimit;
    for (unsigned int i = 0; i < slotLimit; ++i)
    {
        if (slots.find(static_cast<unsigned short>(i)) == slots.end())
        {
            selectedSlot = i;
            break;
        }
    }

    if (selectedSlot == slotLimit)
        return { nullptr, CreationStatus::NoFreeSlot };

    auto player = std::make_unique<Player>(guid);
    player->cell.blank();
    player->npc.blank();
    player->npcStats.blank();
    player->creatureStats.blank();
    player->charClass.blank();
    player->scale = 1;
    player->isWerewolf = false;
    player->setId(static_cast<unsigned short>(selectedSlot));

    Player* result = player.get();
    players.emplace(guid, std::move(player));
    slots.emplace(static_cast<unsigned short>(selectedSlot), result);
    LOG_APPEND(TimedLog::LOG_INFO, "- Storing in slot %i", selectedSlot);
    return { result, CreationStatus::Created };
}

Player *Players::getPlayer(mwmp::transport::TransportConnectionId guid)
{
    auto it = players.find(guid);
    if (it == players.end())
        return nullptr;
    return it->second.get();
}

TPlayers *Players::getPlayers()
{
    return &players;
}

unsigned short Players::getLastPlayerId()
{
    if (slots.empty())
        return 0;
    return slots.rbegin()->first;
}

Player::Player(mwmp::transport::TransportConnectionId guid)
    : BasePlayer(guid)
    , id(std::numeric_limits<unsigned short>::max())
    , loadState(NOTLOADED)
    , handshakeCounter(0)
{
}

Player::~Player()
{

}

unsigned short Player::getId()
{
    return id;
}

void Player::setId(unsigned short id)
{
    this->id = id;
}

bool Player::isHandshaked()
{
    return handshakeCounter == std::numeric_limits<int>::max();
}

void Player::setHandshake()
{
    handshakeCounter = std::numeric_limits<int>::max();
}

void Player::incrementHandshakeAttempts()
{
    handshakeCounter++;
}

int Player::getHandshakeAttempts()
{
    return handshakeCounter;
}


void Player::setLoadState(int state)
{
    loadState = state;
}

int Player::getLoadState()
{
    return loadState;
}

Player *Players::getPlayer(unsigned short id)
{
    auto it = slots.find(id);
    if (it == slots.end())
        return nullptr;
    return it->second;
}

CellController::TContainer *Player::getCells()
{
    return &cells;
}

void Player::sendToLoaded(mwmp::PlayerPacket *myPacket)
{
    std::list <Player*> plList;

    for (auto cell : cells)
        for (auto pl : *cell)
            plList.push_back(pl);

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl == this) continue;
        myPacket->setPlayer(this);
        myPacket->Send(pl->guid);
    }
}

void Player::forEachLoaded(std::function<void(Player *pl, Player *other)> func)
{
    std::list <Player*> plList;

    for (auto cell : cells)
    {
        for (auto pl : *cell)
        {
            if (pl != nullptr && !pl->npc.mName.empty())
                plList.push_back(pl);
        }
    }

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl == this) continue;
        func(this, pl);
    }
}

bool Players::doesPlayerExist(mwmp::transport::TransportConnectionId guid)
{
    const auto it = players.find(guid);
    return it != players.end() && it->second != nullptr;
}
