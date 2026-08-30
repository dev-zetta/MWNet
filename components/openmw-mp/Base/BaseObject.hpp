#ifndef OPENMW_BASEEVENT_HPP
#define OPENMW_BASEEVENT_HPP

#include <components/esm3/loadcell.hpp>
#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <RakNetTypes.h>

namespace mwmp
{
    struct ContainerItem
    {
        std::string refId;
        int count = 0;
        int charge = 0;
        double enchantmentCharge = 0.0;
        std::string soul;

        int actionCount = 0;

        inline bool operator==(const ContainerItem& rhs)
        {
            return refId == rhs.refId && count == rhs.count && charge == rhs.charge &&
                enchantmentCharge == rhs.enchantmentCharge && soul == rhs.soul;
        }
    };

    struct BaseObject
    {
        std::string refId = "";
        unsigned int refNum = 0;
        unsigned int mpNum = 0;
        int count = 0;
        int charge = 0;
        double enchantmentCharge = 0.0;
        std::string soul;
        int goldValue = 0;

        ESM::Position position;

        bool objectState = false;
        int lockLevel = 0;
        float scale = 1.f;

        unsigned char dialogueChoiceType = DialogueChoiceType::TOPIC;
        std::string topicId;
        int guiId = 0;

        std::string soundId;
        float volume = 1.f;
        float pitch = 1.f;

        unsigned int goldPool = 0;
        float lastGoldRestockHour = 0.f;
        int lastGoldRestockDay = 0;


        int doorState = 0;
        bool teleportState = false;
        ESM::Cell destinationCell;
        ESM::Position destinationPosition;

        std::string musicFilename;

        std::string videoFilename;
        bool allowSkipping = false;

        std::string animGroup;
        int animMode = 0;

        bool isDisarmed = false;
        bool droppedByPlayer = false;

        Target activatingActor;
        Target hittingActor;
        Attack hitAttack;

        bool isSummon = false;
        int summonEffectId = 0;
        std::string summonSpellId;
        float summonDuration = 0.f;
        Target master;

        bool hasContainer = false;

        std::vector<ClientVariable> clientLocals;
        std::vector<ContainerItem> containerItems;
        unsigned int containerItemCount = 0;

        RakNet::RakNetGUID guid{}; // only for object lists that can also include players
        bool isPlayer = false;
    };

    class BaseObjectList
    {
    public:

        explicit BaseObjectList(RakNet::RakNetGUID guid)
            : guid(guid)
        {
        }

        BaseObjectList() = default;

        enum WORLD_ACTION
        {
            SET = 0,
            ADD = 1,
            REMOVE = 2,
            REQUEST = 3
        };

        enum CONTAINER_SUBACTION
        {
            NONE = 0,
            DRAG = 1,
            DROP = 2,
            TAKE_ALL = 3,
            REPLY_TO_REQUEST = 4,
            RESTOCK_RESULT = 5
        };

        RakNet::RakNetGUID guid{};
        
        std::vector<BaseObject> baseObjects;
        unsigned int baseObjectCount = 0;

        ESM::Cell cell;
        std::string consoleCommand;

        unsigned char packetOrigin = PACKET_ORIGIN::CLIENT_GAMEPLAY;
        std::string originClientScript;

        unsigned char action = WORLD_ACTION::SET;
        unsigned char containerSubAction = CONTAINER_SUBACTION::NONE;

        bool isValid = false;
    };
}

#endif //OPENMW_BASEEVENT_HPP
