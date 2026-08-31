#ifndef OPENMW_BASEACTOR_HPP
#define OPENMW_BASEACTOR_HPP

#include <components/esm3/loadcell.hpp>

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include <cstdint>

namespace mwmp
{
    class BaseActor
    {
    public:

        BaseActor() = default;

        std::string refId = "";
        unsigned int refNum = 0;
        unsigned int mpNum = 0;

        ESM::Position position{};
        ESM::Position direction{};

        ESM::Cell cell;

        unsigned int movementFlags = 0;
        char drawState = 0;
        bool isFlying = false;

        std::string sound;

        SimpleCreatureStats creatureStats;

        Animation animation;
        char deathState = 0;
        bool isInstantDeath = false;
        Attack attack;
        Cast cast;

        Target killer;

        bool isFollowerCellChange = false;

        bool hasAiTarget = false;
        Target aiTarget;
        unsigned int aiAction = 0;
        unsigned int aiDistance = 0;
        unsigned int aiDuration = 0;
        bool aiShouldRepeat = false;
        ESM::Position aiCoordinates{};

        bool hasPositionData = false;
        bool hasStatsDynamicData = false;

        Item equipmentItems[19];
        SpellsActiveChanges spellsActiveChanges;
    };

    class BaseActorList
    {
    public:

        BaseActorList() = default;

        enum ACTOR_ACTION
        {
            SET = 0,
            ADD = 1,
            REMOVE = 2,
            REQUEST = 3
        };

        enum AI_ACTION
        {
            CANCEL = 0,
            ACTIVATE = 1,
            COMBAT = 2,
            ESCORT = 3,
            FOLLOW = 4,
            TRAVEL = 5,
            WANDER = 6
        };

        mwmp::transport::TransportConnectionId guid{};

        std::uint64_t authorityLeaseId = 0;
        std::uint32_t authorityLeaseDurationMs = 0;

        std::vector<BaseActor> baseActors;

        unsigned int count = 0;

        ESM::Cell cell;

        unsigned char action = ACTOR_ACTION::SET;

        bool isValid = false;
    };
}

#endif //OPENMW_BASEACTOR_HPP
