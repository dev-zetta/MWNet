#ifndef OPENMW_PLAYERLIST_HPP
#define OPENMW_PLAYERLIST_HPP

#include <components/esm3/custommarkerstate.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>

#include "../mwmechanics/aisequence.hpp"

#include "../mwworld/manualref.hpp"

#include "DedicatedPlayer.hpp"

#include <map>
#include <memory>

namespace MWMechanics
{
    class Actor;
}

namespace mwmp
{
    class PlayerList
    {
    public:

        static void update(float dt);

        static DedicatedPlayer *newPlayer(mwmp::transport::TransportConnectionId guid);

        static void deletePlayer(mwmp::transport::TransportConnectionId guid);
        static void cleanUp();

        static DedicatedPlayer *getPlayer(mwmp::transport::TransportConnectionId guid);
        static DedicatedPlayer *getPlayer(const MWWorld::Ptr &ptr);
        static DedicatedPlayer* getPlayer(int actorId);
        static std::vector<mwmp::transport::TransportConnectionId> getPlayersInCell(const ESM::Cell& cell);

        static bool isDedicatedPlayer(const MWWorld::Ptr &ptr);

        static void enableMarkers(const ESM::Cell& cell);

        static void clearHitAttemptActorId(int actorId);

    private:

        static std::map<mwmp::transport::TransportConnectionId, std::unique_ptr<DedicatedPlayer>> playerList;
    };
}

#endif //OPENMW_PLAYERLIST_HPP
