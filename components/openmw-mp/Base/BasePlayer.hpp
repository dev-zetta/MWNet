#ifndef OPENMW_BASEPLAYER_HPP
#define OPENMW_BASEPLAYER_HPP

#include <cstdint>

#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/npcstats.hpp>
#include <components/esm3/creaturestats.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadspel.hpp>

#include <components/openmw-mp/Base/BaseStructs.hpp>

namespace mwmp
{
    enum class JailAction : std::uint8_t
    {
        Begin = 0,
        Complete = 1,
    };

    struct CurrentContainer
    {
        std::string refId;
        unsigned int refNum = 0;
        unsigned int mpNum = 0;
        bool loot = false;
    };

    struct JournalItem
    {
        std::string quest;
        int index = 0;
        enum JOURNAL_ITEM_TYPE
        {
            ENTRY = 0,
            INDEX = 1
        };

        std::string actorRefId;

        bool hasTimestamp = false;
        mwmp::Time timestamp;

        int type = JOURNAL_ITEM_TYPE::ENTRY;
    };

    struct Faction
    {
        std::string factionId;
        int rank = 0;
        int reputation = 0;
        bool isExpelled = false;
    };

    struct Topic
    {
        std::string topicId;
    };

    struct Book
    {
        std::string bookId;
    };

    struct QuickKey
    {
        std::string itemId;

        enum QUICKKEY_TYPE
        {   
            ITEM = 0,
            MAGIC = 1,
            ITEM_MAGIC = 2,
            UNASSIGNED = 3
        };

        unsigned short slot = 0;
        int type = QUICKKEY_TYPE::UNASSIGNED;
    };

    struct CellState
    {
        ESM::Cell cell;

        enum CELL_STATE_ACTION
        {
            LOAD = 0,
            UNLOAD = 1
        };

        int type = CELL_STATE_ACTION::LOAD;
    };

    struct FactionChanges
    {
        std::vector<Faction> factions;

        enum FACTION_ACTION
        {
            RANK = 0,
            EXPULSION = 1,
            REPUTATION = 2
        };

        int action = FACTION_ACTION::RANK;
    };

    struct InventoryChanges
    {
        std::vector<Item> items;
        enum ACTION_TYPE
        {
            SET = 0,
            ADD,
            REMOVE
        };
        int action = ACTION_TYPE::SET;
    };

    struct SpellbookChanges
    {
        std::vector<ESM::Spell> spells;
        enum ACTION_TYPE
        {
            SET = 0,
            ADD,
            REMOVE
        };
        int action = ACTION_TYPE::SET;
    };

    enum RESURRECT_TYPE
    {
        REGULAR = 0,
        IMPERIAL_SHRINE,
        TRIBUNAL_TEMPLE
    };

    enum MISCELLANEOUS_CHANGE_TYPE
    {
        MARK_LOCATION = 0,
        SELECTED_SPELL
    };

    class BasePlayer
    {
    public:

        struct CharGenState
        {
            int currentStage = 0;
            int endStage = 0;
            bool isFinished = false;
        };

        struct GUIMessageBox
        {
            int id = 0;
            int type = GUI_TYPE::MessageBox;
            enum GUI_TYPE
            {
                MessageBox = 0,
                CustomMessageBox,
                InputDialog,
                PasswordDialog,
                ListBox
            };
            std::string label;
            std::string note;
            std::string buttons;

            std::string data;
        };

        explicit BasePlayer(mwmp::transport::TransportConnectionId guid)
            : guid(guid)
        {
        }

        BasePlayer() = default;

        mwmp::transport::TransportConnectionId guid{};

        GUIMessageBox guiMessageBox;

        // Track only the indexes of the attributes that have been changed,
        // with the attribute values themselves being stored in creatureStats.mAttributes
        std::vector<uint8_t> attributeIndexChanges;

        // Track only the indexes of the skills that have been changed,
        // with the skill values themselves being stored in npcStats.mSkills
        std::vector<uint8_t> skillIndexChanges;

        // Track only the indexes of the dynamic states that have been changed,
        // with the dynamicStats themselves being stored in creatureStats.mDynamic
        std::vector<uint8_t> statsDynamicIndexChanges;

        // Track only the indexes of the equipment items that have been changed,
        // with the items themselves being stored in equipmentItems
        std::vector<int> equipmentIndexChanges;

        bool exchangeFullInfo = false;

        InventoryChanges inventoryChanges;
        SpellbookChanges spellbookChanges;
        std::vector<SpellCooldown> cooldownChanges;
        SpellsActiveChanges spellsActiveChanges;
        std::vector<QuickKey> quickKeyChanges;
        std::vector<JournalItem> journalChanges;
        FactionChanges factionChanges;
        std::vector<Topic> topicChanges;
        std::vector<Book> bookChanges;
        std::vector<CellState> cellStateChanges;

        std::vector<mwmp::transport::TransportConnectionId> alliedPlayers;
        CurrentContainer currentContainer;

        int difficulty = 0;
        int enforcedLogLevel = -1;
        float physicsFramerate = 60.0;
        bool consoleAllowed = false;
        bool bedRestAllowed = true;
        bool wildernessRestAllowed = true;
        bool waitAllowed = true;

        bool ignorePosPacket = false;

        unsigned int movementFlags = 0;
        char drawState = 0;
        bool isJumping = false;
        bool isFlying = false;
        bool hasTcl = false;

        ESM::Position position{};
        ESM::Position direction{};
        ESM::Position previousCellPosition{};
        ESM::Position momentum{};
        ESM::Cell cell;
        ESM::NPC npc;
        ESM::NpcStats npcStats;
        ESM::Creature creature;
        ESM::CreatureStats creatureStats;
        ESM::Class charClass;
        Item equipmentItems[19];
        Attack attack;
        Cast cast;
        std::string birthsign;
        std::string chatMessage;
        CharGenState charGenState;
        std::map<std::string, std::string> gameSettings;
        std::map<std::string, std::string> vrSettings;

        std::string sound;
        Animation animation;
        char deathState = 0;

        bool resetStats = false;
        float scale = 1;
        bool isWerewolf = false;

        bool displayCreatureName = false;
        std::string creatureRefId;

        bool isChangingRegion = false;

        Target killer;

        JailAction jailAction = JailAction::Begin;
        std::uint64_t jailSentenceId = 0;
        int jailDays = 0;
        bool ignoreJailTeleportation = false;
        bool ignoreJailSkillIncreases = false;
        std::string jailProgressText;
        std::string jailEndText;

        unsigned int resurrectType = RESURRECT_TYPE::REGULAR;
        unsigned int miscellaneousChangeType = MISCELLANEOUS_CHANGE_TYPE::MARK_LOCATION;

        ESM::Cell markCell;
        ESM::Position markPosition{};
        std::string selectedSpellId;

        mwmp::Item usedItem;
        bool usingItemMagic = false;
        char itemUseDrawState = 0;
    };
}

#endif //OPENMW_BASEPLAYER_HPP
