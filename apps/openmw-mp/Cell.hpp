#ifndef OPENMW_SERVERCELL_HPP
#define OPENMW_SERVERCELL_HPP

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <components/esm/records.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>
#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

class Player;
class Cell;

class Cell
{
    friend class CellController;
public:
    Cell(ESM::Cell cell);
    using TPlayers = std::deque<Player*>;
    using Iterator = TPlayers::const_iterator;

    Iterator begin() const;
    Iterator end() const;

    void addPlayer(Player *player);
    void removePlayer(Player *player, bool cleanPlayer = true);

    void readActorList(unsigned char packetID, const mwmp::BaseActorList *newActorList);
    bool containsActor(int refNum, int mpNum) const;
    mwmp::BaseActor *getActor(int refNum, int mpNum);
    void removeActors(const mwmp::BaseActorList *newActorList);

    RakNet::RakNetGUID *getAuthority();
    void setAuthority(const RakNet::RakNetGUID& guid, std::uint64_t leaseId);
    void clearAuthority();
    std::uint64_t getAuthorityLeaseId() const;
    mwmp::BaseActorList *getActorList();

    TPlayers getPlayers() const;
    void sendToLoaded(mwmp::ActorPacket *actorPacket, mwmp::BaseActorList *baseActorList) const;
    void sendToLoaded(mwmp::ObjectPacket *objectPacket, mwmp::BaseObjectList *baseObjectList) const;

    std::string getShortDescription() const;


private:
    static std::uint64_t actorKey(std::uint32_t refNum, std::uint32_t mpNum) noexcept;
    void rebuildActorIndex();

    TPlayers players;
    ESM::Cell cell;

    RakNet::RakNetGUID authorityGuid{};
    std::uint64_t authorityLeaseId = 0;
    mwmp::BaseActorList cellActorList;
    std::unordered_map<std::uint64_t, std::size_t> actorIndexes;
};


#endif //OPENMW_SERVERCELL_HPP
