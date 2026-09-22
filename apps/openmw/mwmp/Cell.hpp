#ifndef OPENMW_MPCELL_HPP
#define OPENMW_MPCELL_HPP

#include <cstdint>
#include <chrono>
#include <memory>

#include "ActorList.hpp"
#include "LocalActor.hpp"
#include "DedicatedActor.hpp"
#include "../mwworld/cellstore.hpp"

namespace mwmp
{
    class Cell
    {
    public:

        Cell(MWWorld::CellStore* cellStore);
        virtual ~Cell();

        void updateLocal(bool forceUpdate);
        void updateDedicated(float dt);

        void readPositions(ActorList& actorList);
        void readAnimFlags(ActorList& actorList);
        void readAnimPlay(ActorList& actorList);
        void readStatsDynamic(ActorList& actorList);
        void readDeath(ActorList& actorList);
        void readEquipment(ActorList& actorList);
        void readSpeech(ActorList& actorList);
        void readSpellsActive(ActorList& actorList);
        void readAi(ActorList& actorList);
        void readAttack(ActorList& actorList);
        void readCast(ActorList& actorList);
        void readCellChange(ActorList& actorList);

        void initializeLocalActor(const MWWorld::Ptr& ptr);
        void initializeLocalActors();

        void initializeDedicatedActor(const MWWorld::Ptr& ptr);
        void initializeDedicatedActors(ActorList& actorList);

        void uninitializeLocalActors();
        void uninitializeDedicatedActors(ActorList& actorList);
        void uninitializeDedicatedActors();

        virtual LocalActor *getLocalActor(std::string actorIndex);
        virtual DedicatedActor *getDedicatedActor(std::string actorIndex);

        bool hasLocalAuthority();
        bool hasUsableAuthority();
        void setAuthority(const mwmp::transport::TransportConnectionId& guid, std::uint64_t leaseId = 0);
        std::uint64_t getAuthorityLeaseId() const;

        MWWorld::CellStore* getCellStore();
        std::string getShortDescription();

        bool shouldInitializeActors;

    private:
        MWWorld::CellStore* store;
        mwmp::transport::TransportConnectionId authorityGuid{};
        std::uint64_t authorityLeaseId = 0;
        std::chrono::steady_clock::time_point nextAuthorityRenewal{};
        bool awaitingAuthorityRenewal = false;

        std::map<std::string, std::unique_ptr<LocalActor>> localActors;
        std::map<std::string, std::unique_ptr<DedicatedActor>> dedicatedActors;

        float updateTimer;
    };
}

#endif //OPENMW_MPCELL_HPP
