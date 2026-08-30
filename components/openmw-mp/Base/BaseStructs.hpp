#ifndef OPENMW_BASESTRUCTS_HPP
#define OPENMW_BASESTRUCTS_HPP

#include <string>

#include <components/esm3/activespells.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/statstate.hpp>

#include <RakNetTypes.h>

namespace mwmp
{
    namespace DialogueChoiceType
    {
        enum DIALOGUE_CHOICE
        {
            TOPIC,
            PERSUASION,
            COMPANION_SHARE,
            BARTER,
            SPELLS,
            TRAVEL,
            SPELLMAKING,
            ENCHANTING,
            TRAINING,
            REPAIR
        };
    }

    enum PACKET_ORIGIN
    {
        CLIENT_GAMEPLAY = 0,
        CLIENT_CONSOLE = 1,
        CLIENT_DIALOGUE = 2,
        CLIENT_SCRIPT_LOCAL = 3,
        CLIENT_SCRIPT_GLOBAL = 4,
        SERVER_SCRIPT = 5
    };

    enum VARIABLE_TYPE
    {
        SHORT,
        LONG,
        FLOAT,
        INT,
        STRING
    };

    struct ClientVariable
    {
        std::string id;
        int internalIndex = 0;

        char variableType = VARIABLE_TYPE::INT;

        int intValue = 0;
        float floatValue = 0.f;
        std::string stringValue;
    };

    struct Time
    {
        float hour = 0.f;
        int day = 0;
        int month = 0;
        int year = 0;

        int daysPassed = 0;
        float timeScale = 0.f;
    };

    struct Item
    {
        std::string refId;
        int count = 0;
        int charge = 0;
        float enchantmentCharge = 0.f;
        std::string soul;

        inline bool operator==(const Item& rhs)
        {
            return refId == rhs.refId && count == rhs.count && charge == rhs.charge &&
                enchantmentCharge == rhs.enchantmentCharge && soul == rhs.soul;
        }
    };

    struct ProjectileOrigin
    {
        float origin[3]{};
        float orientation[4]{};
    };
    
    struct Target
    {
        bool isPlayer = false;

        std::string refId;
        unsigned int refNum = 0;
        unsigned int mpNum = 0;

        std::string name; // Remove this once the server can get names corresponding to different refIds

        RakNet::RakNetGUID guid{};
    };

    class Attack
    {
    public:

        Target target;

        enum TYPE
        {
            MELEE = 0,
            RANGED
        };

        char type = TYPE::MELEE;
        std::string attackAnimation;

        std::string rangedWeaponId;
        std::string rangedAmmoId;

        ESM::Position hitPosition{};
        ProjectileOrigin projectileOrigin{};

        float damage = 0;
        float attackStrength = 0;

        bool isHit = false;
        bool success = false;
        bool block = false;
        
        bool pressed = false;
        bool instant = false;
        bool knockdown = false;
        bool applyWeaponEnchantment = false;
        bool applyAmmoEnchantment = false;

        bool shouldSend = false;
    };

    class Cast
    {
    public:

        Target target;

        char type = TYPE::REGULAR; // 0 - regular magic, 1 - item magic
        enum TYPE
        {
            REGULAR = 0,
            ITEM
        };

        std::string spellId; // id of spell (e.g. "fireball")
        std::string itemId;

        bool hasProjectile = false;
        ProjectileOrigin projectileOrigin{};

        bool isHit = false;
        bool success = false;
        bool pressed = false;
        bool instant = false;

        bool shouldSend = false;
    };

    struct SpellCooldown
    {
        std::string id;
        int startTimestampDay = 0;
        double startTimestampHour = 0.0;
    };

    struct ActiveSpell
    {
        std::string id;
        bool isStackingSpell = false;
        int timestampDay = 0;
        double timestampHour = 0.0;
        Target caster;
        ESM::ActiveSpells::ActiveSpellParams params;
    };

    struct SpellsActiveChanges
    {
        std::vector<ActiveSpell> activeSpells;
        enum ACTION_TYPE
        {
            SET = 0,
            ADD,
            REMOVE
        };
        int action = ACTION_TYPE::SET; // 0 - Clear and set in entirety, 1 - Add spell, 2 - Remove spell
    };

    struct Animation
    {
        std::string groupname;
        int mode = 0;
        int count = 0;
        bool persist = false;
    };

    struct SimpleCreatureStats
    {
        ESM::StatState<float> mDynamic[3]{};
        bool mDead = false;
        bool mDeathAnimationFinished = false;
    };
}

#endif //OPENMW_BASESTRUCTS_HPP
