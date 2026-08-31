#ifndef OPENMW_COMPONENTS_ESMLOADER_ESMDATA_H
#define OPENMW_COMPONENTS_ESMLOADER_ESMDATA_H

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>
#include <components/vfs/pathutil.hpp>

#include <string_view>
#include <vector>

namespace ESM
{
    struct Activator;
    struct Class;
    struct Creature;
    struct Enchantment;
    struct Ingredient;
    struct MagicEffect;
    struct NPC;
    struct Race;
    struct Skill;
    struct Spell;
    struct Potion;
    struct Cell;
    struct Container;
    struct Door;
    struct GameSetting;
    struct Land;
    struct Static;
    class Variant;
    class RefId;
}

namespace EsmLoader
{
    struct RefIdWithType
    {
        ESM::RefId mId;
        ESM::RecNameInts mType;
    };

    struct EnchantedItem
    {
        ESM::RefId mId;
        ESM::RefId mEnchantment;
        bool mConsumable = false;
    };

    struct EsmData
    {
        std::vector<ESM::Activator> mActivators;
        std::vector<ESM::Cell> mCells;
        std::vector<ESM::Container> mContainers;
        std::vector<ESM::Door> mDoors;
        std::vector<ESM::GameSetting> mGameSettings;
        std::vector<ESM::Class> mClasses;
        std::vector<ESM::Creature> mCreatures;
        std::vector<ESM::Enchantment> mEnchantments;
        std::vector<ESM::Ingredient> mIngredients;
        std::vector<ESM::MagicEffect> mMagicEffects;
        std::vector<ESM::Potion> mPotions;
        std::vector<ESM::Spell> mSpells;
        std::vector<ESM::NPC> mNpcs;
        std::vector<ESM::Race> mRaces;
        std::vector<ESM::Skill> mSkills;
        std::vector<EnchantedItem> mEnchantedItems;
        std::vector<ESM::Land> mLands;
        std::vector<ESM::Static> mStatics;
        std::vector<RefIdWithType> mRefIdTypes;

        EsmData() = default;
        EsmData(const EsmData&) = delete;
        EsmData(EsmData&&) = default;

        ~EsmData();
    };

    VFS::Path::NormalizedView getModel(const EsmData& content, const ESM::RefId& refId, ESM::RecNameInts type);

    ESM::Variant getGameSetting(const std::vector<ESM::GameSetting>& records, std::string_view id);
}

#endif
