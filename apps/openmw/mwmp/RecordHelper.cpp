#include <components/openmw-mp/TimedLog.hpp>

#include "../mwworld/cellstore.hpp"
#include "../mwworld/worldimp.hpp"

#include "RecordHelper.hpp"
#include "Main.hpp"
#include "CellController.hpp"
#include "Cell.hpp"

void RecordHelper::overrideRecord(const mwmp::ActivatorRecord& record)
{
    const ESM::Activator &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Activator>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Activator>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Activator *baseData = world->getStore().get<ESM::Activator>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Activator finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::ApparatusRecord& record)
{
    const ESM::Apparatus &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Apparatus>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Apparatus>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Apparatus *baseData = world->getStore().get<ESM::Apparatus>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Apparatus finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasQuality)
            finalData.mData.mQuality = recordData.mData.mQuality;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::ArmorRecord& record)
{
    const ESM::Armor &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Armor>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        if (!recordData.mEnchant.empty() && !doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
        {
            LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring new armor record with invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
            return;
        }
        else
            world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Armor>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Armor *baseData = world->getStore().get<ESM::Armor>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Armor finalData = *baseData;
        finalData.mId = recordData.mId;

        finalData.mParts.mParts.at(0);

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasHealth)
            finalData.mData.mHealth = recordData.mData.mHealth;

        if (record.baseOverrides.hasArmorRating)
            finalData.mData.mArmor = recordData.mData.mArmor;

        if (record.baseOverrides.hasEnchantmentId)
        {
            if (recordData.mEnchant.empty() || doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
                finalData.mEnchant = recordData.mEnchant;
            else
                LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
        }

        if (record.baseOverrides.hasEnchantmentCharge)
            finalData.mData.mEnchant = recordData.mData.mEnchant;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        if (record.baseOverrides.hasBodyParts)
            finalData.mParts.mParts = recordData.mParts.mParts;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::BodyPartRecord& record)
{
    const ESM::BodyPart &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::BodyPart>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::BodyPart *baseData = world->getStore().get<ESM::BodyPart>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::BodyPart finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasRace)
            finalData.mRace = recordData.mRace;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasBodyPartType)
            finalData.mData.mPart = recordData.mData.mPart;

        if (record.baseOverrides.hasVampireState)
            finalData.mData.mVampire = recordData.mData.mVampire;

        if (record.baseOverrides.hasFlags)
            finalData.mData.mFlags = recordData.mData.mFlags;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }
}

void RecordHelper::overrideRecord(const mwmp::BookRecord& record)
{
    const ESM::Book &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Book>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        if (!recordData.mEnchant.empty() && !doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
        {
            LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring new book record with invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
            return;
        }
        else
            world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Book>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Book *baseData = world->getStore().get<ESM::Book>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Book finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasText)
            finalData.mText = recordData.mText;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasScrollState)
            finalData.mData.mIsScroll = recordData.mData.mIsScroll;

        if (record.baseOverrides.hasSkillId)
            finalData.mData.mSkillId = recordData.mData.mSkillId;

        if (record.baseOverrides.hasEnchantmentId)
        {
            if (recordData.mEnchant.empty() || doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
                finalData.mEnchant = recordData.mEnchant;
            else
                LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
        }

        if (record.baseOverrides.hasEnchantmentCharge)
            finalData.mData.mEnchant = recordData.mData.mEnchant;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::CellRecord& record)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();

    ESM::Cell recordData = record.data;

    if (recordData.mName.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    MWWorld::Ptr playerPtr = world->getPlayerPtr();
    ESM::Cell playerCell = playerPtr.getCell()->getCell()->getEsm3();
    ESM::Position playerPos = playerPtr.getRefData().getPosition();

    bool isActiveCell = world->isCellActive(recordData);

    if (isActiveCell)
    {
        mwmp::Main::get().getCellController()->uninitializeCell(recordData);

        // Change to temporary holding interior cell
        world->changeToInteriorCell(RecordHelper::getPlaceholderInteriorCellName(), playerPos, true, true);
    }

    if (record.baseId.empty())
    {
        recordData.mData.mFlags |= ESM::Cell::Flags::Interior;

        world->unloadCell(recordData);
        world->clearCellStore(recordData);
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Cell>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Cell *baseData = world->getStore().get<ESM::Cell>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Cell finalData = *baseData;
        finalData.mName = recordData.mName;

        world->unloadCell(finalData);
        world->clearCellStore(finalData);
        world->getModifiableStore().overrideRecord(finalData);

        // Create a Pathgrid record for this new Cell based on the base Cell's Pathgrid
        // Note: This has to be done after the new Cell has been created so the Pathgrid override
        //       can correctly determine whether the Cell is an interior or an exterior
        const ESM::Pathgrid* basePathgrid = world->getStore().get<ESM::Pathgrid>().search(ESM::RefId::stringRefId(record.baseId));

        // Pathgrid override skipped - Store<ESM::Pathgrid> uses mCell not mId
        (void)basePathgrid;
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    // Move the player back to the cell they were in
    if (isActiveCell)
    {
        if (playerCell.isExterior())
            world->changeToCell(ESM::RefId::esm3ExteriorCell(playerCell.getGridX(), playerCell.getGridY()), playerPos, true);
        else
            world->changeToInteriorCell(playerCell.mName, playerPos, true, true);
    }
}

void RecordHelper::overrideRecord(const mwmp::ClothingRecord& record)
{
    const ESM::Clothing &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Clothing>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        if (!recordData.mEnchant.empty() && !doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
        {
            LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring new clothing record with invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
            return;
        }
        else
            world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Clothing>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Clothing *baseData = world->getStore().get<ESM::Clothing>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Clothing finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasEnchantmentId)
        {
            if (recordData.mEnchant.empty() || doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
                finalData.mEnchant = recordData.mEnchant;
            else
                LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
        }

        if (record.baseOverrides.hasEnchantmentCharge)
            finalData.mData.mEnchant = recordData.mData.mEnchant;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        if (record.baseOverrides.hasBodyParts)
            finalData.mParts.mParts = recordData.mParts.mParts;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::ContainerRecord& record)
{
    const ESM::Container &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Container>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Container>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Container *baseData = world->getStore().get<ESM::Container>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Container finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasWeight)
            finalData.mWeight = recordData.mWeight;

        if (record.baseOverrides.hasFlags)
            finalData.mFlags = recordData.mFlags;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        if (record.baseOverrides.hasInventory)
            finalData.mInventory.mList = recordData.mInventory.mList;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::CreatureRecord& record)
{
    const ESM::Creature &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Creature>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Creature>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Creature *baseData = world->getStore().get<ESM::Creature>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Creature finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasScale)
            finalData.mScale = recordData.mScale;

        if (record.baseOverrides.hasBloodType)
            finalData.mBloodType = recordData.mBloodType;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasLevel)
            finalData.mData.mLevel = recordData.mData.mLevel;

        if (record.baseOverrides.hasHealth)
            finalData.mData.mHealth = recordData.mData.mHealth;

        if (record.baseOverrides.hasMagicka)
            finalData.mData.mMana = recordData.mData.mMana;

        if (record.baseOverrides.hasFatigue)
            finalData.mData.mFatigue = recordData.mData.mFatigue;

        if (record.baseOverrides.hasSoulValue)
            finalData.mData.mSoul = recordData.mData.mSoul;

        if (record.baseOverrides.hasDamageChop)
        {
            finalData.mData.mAttack[0] = recordData.mData.mAttack[0];
            finalData.mData.mAttack[1] = recordData.mData.mAttack[1];
        }

        if (record.baseOverrides.hasDamageSlash)
        {
            finalData.mData.mAttack[2] = recordData.mData.mAttack[2];
            finalData.mData.mAttack[3] = recordData.mData.mAttack[3];
        }

        if (record.baseOverrides.hasDamageThrust)
        {
            finalData.mData.mAttack[4] = recordData.mData.mAttack[4];
            finalData.mData.mAttack[5] = recordData.mData.mAttack[5];
        }

        if (record.baseOverrides.hasAiFight)
            finalData.mAiData.mFight = recordData.mAiData.mFight;

        if (record.baseOverrides.hasAiFlee)
            finalData.mAiData.mFlee = recordData.mAiData.mFlee;

        if (record.baseOverrides.hasAiAlarm)
            finalData.mAiData.mAlarm = recordData.mAiData.mAlarm;

        if (record.baseOverrides.hasAiServices)
            finalData.mAiData.mServices = recordData.mAiData.mServices;

        if (record.baseOverrides.hasFlags)
            finalData.mFlags = recordData.mFlags;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        if (!record.inventoryBaseId.empty() && doesRecordIdExist<ESM::Creature>(ESM::RefId::stringRefId(record.inventoryBaseId)))
            finalData.mInventory.mList = world->getStore().get<ESM::Creature>().search(ESM::RefId::stringRefId(record.inventoryBaseId))->mInventory.mList;
        else if (record.baseOverrides.hasInventory)
            finalData.mInventory.mList = recordData.mInventory.mList;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::DoorRecord& record)
{
    const ESM::Door &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Door>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Door>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Door *baseData = world->getStore().get<ESM::Door>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Door finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasOpenSound)
            finalData.mOpenSound = recordData.mOpenSound;

        if (record.baseOverrides.hasCloseSound)
            finalData.mCloseSound = recordData.mCloseSound;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::EnchantmentRecord& record)
{
    const ESM::Enchantment &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        if (recordData.mEffects.mList.empty())
        {
            LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring new enchantment record with no effects");
            return;
        }
        else
            world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Enchantment>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Enchantment *baseData = world->getStore().get<ESM::Enchantment>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Enchantment finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasCost)
            finalData.mData.mCost = recordData.mData.mCost;

        if (record.baseOverrides.hasCharge)
            finalData.mData.mCharge = recordData.mData.mCharge;

        if (record.baseOverrides.hasFlags)
            finalData.mData.mFlags = recordData.mData.mFlags;

        if (record.baseOverrides.hasEffects)
            finalData.mEffects.mList = recordData.mEffects.mList;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }
}

void RecordHelper::overrideRecord(const mwmp::GameSettingRecord& record)
{
    const ESM::GameSetting& recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    MWBase::World* world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::GameSetting>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::GameSetting* baseData = world->getStore().get<ESM::GameSetting>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::GameSetting finalData = *baseData;
        finalData.mId = recordData.mId;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }
}

void RecordHelper::overrideRecord(const mwmp::IngredientRecord& record)
{
    const ESM::Ingredient &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Ingredient>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Ingredient>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Ingredient *baseData = world->getStore().get<ESM::Ingredient>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Ingredient finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        if (record.baseOverrides.hasEffects)
        {
            const static unsigned int effectCap = sizeof(recordData.mData.mEffectID) / sizeof(recordData.mData.mEffectID[0]);

            for (int effectIndex = 0; effectIndex < effectCap; effectIndex++)
            {
                finalData.mData.mEffectID[effectIndex] = recordData.mData.mEffectID[effectIndex];
                finalData.mData.mAttributes[effectIndex] = recordData.mData.mAttributes[effectIndex];
                finalData.mData.mSkills[effectIndex] = recordData.mData.mSkills[effectIndex];
            }
        }

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::LightRecord& record)
{
    const ESM::Light &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Light>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Light>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Light *baseData = world->getStore().get<ESM::Light>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Light finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasSound)
            finalData.mSound = recordData.mSound;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasTime)
            finalData.mData.mTime = recordData.mData.mTime;

        if (record.baseOverrides.hasRadius)
            finalData.mData.mRadius = recordData.mData.mRadius;

        if (record.baseOverrides.hasColor)
            finalData.mData.mColor = recordData.mData.mColor;

        if (record.baseOverrides.hasFlags)
            finalData.mData.mFlags = recordData.mData.mFlags;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::LockpickRecord& record)
{
    const ESM::Lockpick &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Lockpick>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Lockpick>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Lockpick *baseData = world->getStore().get<ESM::Lockpick>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Lockpick finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasQuality)
            finalData.mData.mQuality = recordData.mData.mQuality;

        if (record.baseOverrides.hasUses)
            finalData.mData.mUses = recordData.mData.mUses;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::MiscellaneousRecord& record)
{
    const ESM::Miscellaneous &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Miscellaneous>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Miscellaneous>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Miscellaneous *baseData = world->getStore().get<ESM::Miscellaneous>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Miscellaneous finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasKeyState)
        {
            if (recordData.mData.mFlags & ESM::Miscellaneous::Key)
                finalData.mData.mFlags |= ESM::Miscellaneous::Key;
            else
                finalData.mData.mFlags &= ~ESM::Miscellaneous::Key;
        }

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::NpcRecord& record)
{
    const ESM::NPC &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::NPC>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        if (!doesRecordIdExist<ESM::Race>(recordData.mRace))
        {
            LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring new NPC record with invalid race provided");
            return;
        }
        else if (!doesRecordIdExist<ESM::Class>(recordData.mClass))
        {
            LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring new NPC record with invalid class provided");
            return;
        }
        else
            world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::NPC>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::NPC *baseData = world->getStore().get<ESM::NPC>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::NPC finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasFlags)
            finalData.mFlags = recordData.mFlags;

        // Because the gender is part of mFlags and can easily be set incorrectly there,
        // we handle it separately here
        if (record.baseOverrides.hasGender)
            finalData.setIsMale(recordData.isMale());
        else
            finalData.setIsMale(baseData->isMale());

        if (record.baseOverrides.hasRace)
            finalData.mRace = recordData.mRace;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasHair)
            finalData.mHair = recordData.mHair;

        if (record.baseOverrides.hasHead)
            finalData.mHead = recordData.mHead;

        if (!recordData.mClass.empty())
            finalData.mClass = recordData.mClass;

        if (record.baseOverrides.hasFaction)
            finalData.mFaction = recordData.mFaction;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        if (record.baseOverrides.hasLevel)
            finalData.mNpdt.mLevel = recordData.mNpdt.mLevel;

        if (record.baseOverrides.hasHealth)
            finalData.mNpdt.mHealth = recordData.mNpdt.mHealth;

        if (record.baseOverrides.hasMagicka)
            finalData.mNpdt.mMana = recordData.mNpdt.mMana;

        if (record.baseOverrides.hasFatigue)
            finalData.mNpdt.mFatigue = recordData.mNpdt.mFatigue;

        if (record.baseOverrides.hasAiFight)
            finalData.mAiData.mFight = recordData.mAiData.mFight;

        if (record.baseOverrides.hasAiFlee)
            finalData.mAiData.mFlee = recordData.mAiData.mFlee;

        if (record.baseOverrides.hasAiAlarm)
            finalData.mAiData.mAlarm = recordData.mAiData.mAlarm;

        if (record.baseOverrides.hasAiServices)
            finalData.mAiData.mServices = recordData.mAiData.mServices;

        if (record.baseOverrides.hasFlags)
            finalData.mFlags = recordData.mFlags;

        if (record.baseOverrides.hasAutoCalc)
        {
            finalData.mNpdtType = recordData.mNpdtType;

            if ((recordData.mFlags & ESM::NPC::Autocalc) != 0)
                finalData.mFlags |= ESM::NPC::Autocalc;
            else
                finalData.mFlags &= ~ESM::NPC::Autocalc;
        }

        if (!record.inventoryBaseId.empty() && doesRecordIdExist<ESM::NPC>(ESM::RefId::stringRefId(record.inventoryBaseId)))
            finalData.mInventory.mList = world->getStore().get<ESM::NPC>().search(ESM::RefId::stringRefId(record.inventoryBaseId))->mInventory.mList;
        else if (record.baseOverrides.hasInventory)
            finalData.mInventory.mList = recordData.mInventory.mList;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::PotionRecord& record)
{
    const ESM::Potion &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Potion>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Potion>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Potion *baseData = world->getStore().get<ESM::Potion>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Potion finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasAutoCalc)
        {
            // mAutoCalc removed; flags-based approach not applicable for potions
        }

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        if (record.baseOverrides.hasEffects)
            finalData.mEffects.mList = recordData.mEffects.mList;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::ProbeRecord& record)
{
    const ESM::Probe &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Probe>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Probe>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Probe *baseData = world->getStore().get<ESM::Probe>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Probe finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasQuality)
            finalData.mData.mQuality = recordData.mData.mQuality;

        if (record.baseOverrides.hasUses)
            finalData.mData.mUses = recordData.mData.mUses;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::RepairRecord& record)
{
    const ESM::Repair &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Repair>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Repair>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Repair *baseData = world->getStore().get<ESM::Repair>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Repair finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasQuality)
            finalData.mData.mQuality = recordData.mData.mQuality;

        if (record.baseOverrides.hasUses)
            finalData.mData.mUses = recordData.mData.mUses;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::ScriptRecord& record)
{
    const ESM::Script &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Script>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Script *baseData = world->getStore().get<ESM::Script>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Script finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasScriptText)
            finalData.mScriptText = recordData.mScriptText;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }
}

void RecordHelper::overrideRecord(const mwmp::SoundRecord& record)
{
    const ESM::Sound& recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Sound>(recordData.mId);
    MWBase::World* world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Sound>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Sound* baseData = world->getStore().get<ESM::Sound>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Sound finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasSound)
            finalData.mSound = recordData.mSound;

        if (record.baseOverrides.hasVolume)
            finalData.mData.mVolume = recordData.mData.mVolume;

        if (record.baseOverrides.hasMinRange)
            finalData.mData.mMinRange = recordData.mData.mMinRange;

        if (record.baseOverrides.hasMaxRange)
            finalData.mData.mMaxRange = recordData.mData.mMaxRange;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::SpellRecord& record)
{
    const ESM::Spell &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Spell>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Spell>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Spell *baseData = world->getStore().get<ESM::Spell>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Spell finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasCost)
            finalData.mData.mCost = recordData.mData.mCost;

        if (record.baseOverrides.hasFlags)
            finalData.mData.mFlags = recordData.mData.mFlags;

        if (record.baseOverrides.hasEffects)
            finalData.mEffects.mList = recordData.mEffects.mList;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }
}

void RecordHelper::overrideRecord(const mwmp::StaticRecord& record)
{
    const ESM::Static &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Static>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Static>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Static *baseData = world->getStore().get<ESM::Static>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Static finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::overrideRecord(const mwmp::WeaponRecord& record)
{
    const ESM::Weapon &recordData = record.data;

    if (recordData.mId.empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with no id provided");
        return;
    }

    bool isExistingId = doesRecordIdExist<ESM::Weapon>(recordData.mId);
    MWBase::World *world = MWBase::Environment::get().getWorld();

    if (record.baseId.empty())
    {
        if (!recordData.mEnchant.empty() && !doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
        {
            LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring new weapon record with invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
            return;
        }
        else
            world->getModifiableStore().overrideRecord(recordData);
    }
    else if (doesRecordIdExist<ESM::Weapon>(ESM::RefId::stringRefId(record.baseId)))
    {
        const ESM::Weapon *baseData = world->getStore().get<ESM::Weapon>().search(ESM::RefId::stringRefId(record.baseId));
        ESM::Weapon finalData = *baseData;
        finalData.mId = recordData.mId;

        if (record.baseOverrides.hasName)
            finalData.mName = recordData.mName;

        if (record.baseOverrides.hasModel)
            finalData.mModel = recordData.mModel;

        if (record.baseOverrides.hasIcon)
            finalData.mIcon = recordData.mIcon;

        if (record.baseOverrides.hasSubtype)
            finalData.mData.mType = recordData.mData.mType;

        if (record.baseOverrides.hasWeight)
            finalData.mData.mWeight = recordData.mData.mWeight;

        if (record.baseOverrides.hasValue)
            finalData.mData.mValue = recordData.mData.mValue;

        if (record.baseOverrides.hasHealth)
            finalData.mData.mHealth = recordData.mData.mHealth;

        if (record.baseOverrides.hasSpeed)
            finalData.mData.mSpeed = recordData.mData.mSpeed;

        if (record.baseOverrides.hasReach)
            finalData.mData.mReach = recordData.mData.mReach;

        if (record.baseOverrides.hasDamageChop)
        {
            finalData.mData.mChop[0] = recordData.mData.mChop[0];
            finalData.mData.mChop[1] = recordData.mData.mChop[1];
        }

        if (record.baseOverrides.hasDamageSlash)
        {
            finalData.mData.mSlash[0] = recordData.mData.mSlash[0];
            finalData.mData.mSlash[1] = recordData.mData.mSlash[1];
        }

        if (record.baseOverrides.hasDamageThrust)
        {
            finalData.mData.mThrust[0] = recordData.mData.mThrust[0];
            finalData.mData.mThrust[1] = recordData.mData.mThrust[1];
        }

        if (record.baseOverrides.hasFlags)
            finalData.mData.mFlags = recordData.mData.mFlags;

        if (record.baseOverrides.hasEnchantmentId)
        {
            if (recordData.mEnchant.empty() || doesRecordIdExist<ESM::Enchantment>(recordData.mEnchant))
                finalData.mEnchant = recordData.mEnchant;
            else
                LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring invalid enchantmentId %s", recordData.mEnchant.getRefIdString().c_str());
        }

        if (record.baseOverrides.hasEnchantmentCharge)
            finalData.mData.mEnchant = recordData.mData.mEnchant;

        if (record.baseOverrides.hasScript)
            finalData.mScript = recordData.mScript;

        world->getModifiableStore().overrideRecord(finalData);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "-- Ignoring record override with invalid baseId %s", record.baseId.c_str());
        return;
    }

    if (isExistingId)
        world->updatePtrsWithRefId(recordData.mId);
}

void RecordHelper::createPlaceholderInteriorCell()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();

    ESM::Cell placeholderInterior;
    placeholderInterior.mData.mFlags |= ESM::Cell::Flags::Interior;
    placeholderInterior.mName = placeholderInteriorCellName;

    world->getModifiableStore().insert(placeholderInterior);
}

const std::string RecordHelper::getPlaceholderInteriorCellName()
{
    return placeholderInteriorCellName;
}
