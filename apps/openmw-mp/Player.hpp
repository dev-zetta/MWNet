#ifndef OPENMW_PLAYER_HPP
#define OPENMW_PLAYER_HPP

#include <map>
#include <memory>
#include <string>
#include <chrono>

#include <components/esm3/npcstats.hpp>
#include <components/esm3/cellid.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcell.hpp>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>
#include "Cell.hpp"
#include "CellController.hpp"

using TPlayers = std::map<mwmp::transport::TransportConnectionId, std::unique_ptr<Player>>;
using TSlots = std::map<unsigned short, Player*>;

class Players
{
public:
    enum class CreationStatus
    {
        Created,
        AlreadyExists,
        NoFreeSlot
    };

    struct CreationResult
    {
        Player* player = nullptr;
        CreationStatus status = CreationStatus::NoFreeSlot;

        explicit operator bool() const { return status == CreationStatus::Created; }
    };

    static CreationResult newPlayer(mwmp::transport::TransportConnectionId guid, unsigned int maximumPlayers);
    static bool deletePlayer(mwmp::transport::TransportConnectionId guid);
    static Player *getPlayer(mwmp::transport::TransportConnectionId guid);
    static Player *getPlayer(unsigned short id);
    static TPlayers *getPlayers();
    static unsigned short getLastPlayerId();
    static bool doesPlayerExist(mwmp::transport::TransportConnectionId guid);

private:
    static TPlayers players;
    static TSlots slots;
};

class Player : public mwmp::BasePlayer
{
    friend class Cell;
    unsigned short id;
public:

    enum
    {
        NOTLOADED=0,
        LOADED,
        POSTLOADED,
        KICKED
    };
    Player(mwmp::transport::TransportConnectionId guid);

    unsigned short getId();
    void setId(unsigned short id);

    bool isHandshaked();
    int getHandshakeAttempts();
    void incrementHandshakeAttempts();
    void setHandshake();

    void setLoadState(int state);
    int getLoadState();

    virtual ~Player();

    CellController::TContainer *getCells();
    void sendToLoaded(mwmp::PlayerPacket *myPacket);

    void forEachLoaded(std::function<void(Player *pl, Player *other)> func);

private:
    CellController::TContainer cells;
    int loadState;
    int handshakeCounter;

};

#endif //OPENMW_PLAYER_HPP
