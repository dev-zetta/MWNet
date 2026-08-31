#include "PacketRecordDynamic.hpp"

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Utils.hpp>

using namespace mwmp;

PacketRecordDynamic::PacketRecordDynamic() : WorldstatePacket()
{
    packetID = ID_RECORD_DYNAMIC;
}

void PacketRecordDynamic::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    Field(worldstate->recordsType);

    if (send)
    {
        // These can be created by players through gameplay and should be checked first
        if (worldstate->recordsType == mwmp::RECORD_TYPE::SPELL)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->spellRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::POTION)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->potionRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::ENCHANTMENT)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->enchantmentRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::ARMOR)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->armorRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::BOOK)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->bookRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CLOTHING)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->clothingRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::MISCELLANEOUS)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->miscellaneousRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::WEAPON)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->weaponRecords);
        // These can only be created by the server
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::ACTIVATOR)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->activatorRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::APPARATUS)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->apparatusRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::BODYPART)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->bodyPartRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CELL)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->cellRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CONTAINER)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->containerRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CREATURE)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->creatureRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::DOOR)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->doorRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::GAMESETTING)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->gameSettingRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::INGREDIENT)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->ingredientRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::LIGHT)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->lightRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::LOCKPICK)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->lockpickRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::NPC)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->npcRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::PROBE)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->probeRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::REPAIR)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->repairRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::SCRIPT)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->scriptRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::STATIC)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->staticRecords);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::SOUND)
            worldstate->recordsCount = Utils::getVectorSize(worldstate->soundRecords);
        else
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Processed invalid ID_RECORD_DYNAMIC packet about unimplemented recordsType %i",
                worldstate->recordsType);
            return;
        }
    }

    if (!CollectionSize(worldstate->recordsCount, maxRecords))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Processed invalid ID_RECORD_DYNAMIC packet with %i records, above the maximum of %i",
            worldstate->recordsCount, maxRecords);
        LOG_APPEND(TimedLog::LOG_ERROR, "- The packet was ignored after that point");
        return;
    }

    if (!send)
    {
        if (worldstate->recordsType == mwmp::RECORD_TYPE::SPELL)
            Utils::resetVector(worldstate->spellRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::POTION)
            Utils::resetVector(worldstate->potionRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::ENCHANTMENT)
            Utils::resetVector(worldstate->enchantmentRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::ARMOR)
            Utils::resetVector(worldstate->armorRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::BOOK)
            Utils::resetVector(worldstate->bookRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CLOTHING)
            Utils::resetVector(worldstate->clothingRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::MISCELLANEOUS)
            Utils::resetVector(worldstate->miscellaneousRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::WEAPON)
            Utils::resetVector(worldstate->weaponRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::ACTIVATOR)
            Utils::resetVector(worldstate->activatorRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::APPARATUS)
            Utils::resetVector(worldstate->apparatusRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::BODYPART)
            Utils::resetVector(worldstate->bodyPartRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CELL)
            Utils::resetVector(worldstate->cellRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CONTAINER)
            Utils::resetVector(worldstate->containerRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::CREATURE)
            Utils::resetVector(worldstate->creatureRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::DOOR)
            Utils::resetVector(worldstate->doorRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::GAMESETTING)
            Utils::resetVector(worldstate->gameSettingRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::INGREDIENT)
            Utils::resetVector(worldstate->ingredientRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::LOCKPICK)
            Utils::resetVector(worldstate->lockpickRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::LIGHT)
            Utils::resetVector(worldstate->lightRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::NPC)
            Utils::resetVector(worldstate->npcRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::PROBE)
            Utils::resetVector(worldstate->probeRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::REPAIR)
            Utils::resetVector(worldstate->repairRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::SCRIPT)
            Utils::resetVector(worldstate->scriptRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::SOUND)
            Utils::resetVector(worldstate->soundRecords, worldstate->recordsCount);
        else if (worldstate->recordsType == mwmp::RECORD_TYPE::STATIC)
            Utils::resetVector(worldstate->staticRecords, worldstate->recordsCount);
    }

    if (worldstate->recordsType == mwmp::RECORD_TYPE::SPELL)
    {
        for (auto &&record : worldstate->spellRecords)
        {
            auto &&recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mData.mType);
            Field(recordData.mData.mCost);
            Field(recordData.mData.mFlags);
            ProcessEffects(recordData.mEffects, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasSubtype);
                Field(overrides.hasCost);
                Field(overrides.hasFlags);
                Field(overrides.hasEffects);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::POTION)
    {
        for (auto &&record : worldstate->potionRecords)
        {
            auto &recordData = record.data;

            Field(record.quantity);
            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mWeight);
            Field(recordData.mData.mValue);
            Field(recordData.mData.mFlags);
            Field(recordData.mScript, true);
            ProcessEffects(recordData.mEffects, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasAutoCalc);
                Field(overrides.hasScript);
                Field(overrides.hasEffects);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::ENCHANTMENT)
    {
        for (auto &&record : worldstate->enchantmentRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mData.mType);
            Field(recordData.mData.mCost);
            Field(recordData.mData.mCharge);
            Field(recordData.mData.mFlags);
            ProcessEffects(recordData.mEffects, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasSubtype);
                Field(overrides.hasCost);
                Field(overrides.hasCharge);
                Field(overrides.hasFlags);
                Field(overrides.hasEffects);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::ARMOR)
    {
        for (auto &&record : worldstate->armorRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mType);
            Field(recordData.mData.mWeight);
            Field(recordData.mData.mValue);
            Field(recordData.mData.mHealth);
            Field(recordData.mData.mArmor);
            Field(recordData.mData.mEnchant);
            Field(recordData.mEnchant, true);
            Field(recordData.mScript, true);
            ProcessBodyParts(recordData.mParts, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasSubtype);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasHealth);
                Field(overrides.hasArmorRating);
                Field(overrides.hasEnchantmentCharge);
                Field(overrides.hasEnchantmentId);
                Field(overrides.hasScript);
                Field(overrides.hasBodyParts);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::BOOK)
    {
        for (auto &&record : worldstate->bookRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mText, true);
            Field(recordData.mData.mWeight);
            Field(recordData.mData.mValue);
            Field(recordData.mData.mIsScroll);
            Field(recordData.mData.mSkillId);
            Field(recordData.mData.mEnchant);
            Field(recordData.mEnchant, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasText);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasScrollState);
                Field(overrides.hasSkillId);
                Field(overrides.hasEnchantmentCharge);
                Field(overrides.hasEnchantmentId);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::CLOTHING)
    {
        for (auto &&record : worldstate->clothingRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mType);
            Field(recordData.mData.mWeight);
            Field(recordData.mData.mValue);
            Field(recordData.mData.mEnchant);
            Field(recordData.mEnchant, true);
            Field(recordData.mScript, true);
            ProcessBodyParts(recordData.mParts, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasSubtype);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasEnchantmentCharge);
                Field(overrides.hasEnchantmentId);
                Field(overrides.hasScript);
                Field(overrides.hasBodyParts);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::MISCELLANEOUS)
    {
        for (auto &&record : worldstate->miscellaneousRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mWeight);
            Field(recordData.mData.mValue);
            Field(recordData.mData.mFlags);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasKeyState);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::WEAPON)
    {
        for (auto &&record : worldstate->weaponRecords)
        {
            auto &recordData = record.data;

            Field(record.quantity);
            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mType);
            Field(recordData.mData.mWeight);
            Field(recordData.mData.mValue);
            Field(recordData.mData.mHealth);
            Field(recordData.mData.mSpeed);
            Field(recordData.mData.mReach);
            Field(recordData.mData.mChop[0]);
            Field(recordData.mData.mChop[1]);
            Field(recordData.mData.mSlash[0]);
            Field(recordData.mData.mSlash[1]);
            Field(recordData.mData.mThrust[0]);
            Field(recordData.mData.mThrust[1]);
            Field(recordData.mData.mFlags);
            Field(recordData.mData.mEnchant);
            Field(recordData.mEnchant, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasSubtype);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasHealth);
                Field(overrides.hasSpeed);
                Field(overrides.hasReach);
                Field(overrides.hasDamageChop);
                Field(overrides.hasDamageSlash);
                Field(overrides.hasDamageThrust);
                Field(overrides.hasFlags);
                Field(overrides.hasEnchantmentCharge);
                Field(overrides.hasEnchantmentId);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::ACTIVATOR)
    {
        for (auto &&record : worldstate->activatorRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::APPARATUS)
    {
        for (auto &&record : worldstate->apparatusRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mType, true);
            Field(recordData.mData.mWeight, true);
            Field(recordData.mData.mValue, true);
            Field(recordData.mData.mQuality, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasSubtype);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasQuality);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::BODYPART)
    {
        for (auto &&record : worldstate->bodyPartRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mModel, true);
            Field(recordData.mRace, true);
            Field(recordData.mData.mType);
            Field(recordData.mData.mPart);
            Field(recordData.mData.mVampire);
            Field(recordData.mData.mFlags);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasModel);
                Field(overrides.hasRace);
                Field(overrides.hasSubtype);
                Field(overrides.hasBodyPartType);
                Field(overrides.hasVampireState);
                Field(overrides.hasFlags);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::CELL)
    {
        for (auto &&record : worldstate->cellRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mName, true);
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::CONTAINER)
    {
        for (auto &&record : worldstate->containerRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mWeight);
            Field(recordData.mFlags);
            Field(recordData.mScript, true);
            ProcessInventoryList(record.inventory, recordData.mInventory, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasWeight);
                Field(overrides.hasFlags);
                Field(overrides.hasScript);
                Field(overrides.hasInventory);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::CREATURE)
    {
        for (auto &&record : worldstate->creatureRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(record.inventoryBaseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mScale);
            Field(recordData.mBloodType);
            Field(recordData.mData.mType);
            Field(recordData.mData.mLevel);
            Field(recordData.mData.mHealth);
            Field(recordData.mData.mMana);
            Field(recordData.mData.mFatigue);
            Field(recordData.mData.mSoul);
            Field(recordData.mData.mAttack[0]);
            Field(recordData.mData.mAttack[1]);
            Field(recordData.mData.mAttack[2]);
            Field(recordData.mData.mAttack[3]);
            Field(recordData.mData.mAttack[4]);
            Field(recordData.mData.mAttack[5]);
            Field(recordData.mAiData.mFight);
            Field(recordData.mAiData.mFlee);
            Field(recordData.mAiData.mAlarm);
            Field(recordData.mAiData.mServices);
            Field(recordData.mFlags);
            Field(recordData.mScript, true);
            ProcessInventoryList(record.inventory, recordData.mInventory, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasScale);
                Field(overrides.hasBloodType);
                Field(overrides.hasSubtype);
                Field(overrides.hasLevel);
                Field(overrides.hasHealth);
                Field(overrides.hasMagicka);
                Field(overrides.hasFatigue);
                Field(overrides.hasSoulValue);
                Field(overrides.hasDamageChop);
                Field(overrides.hasDamageSlash);
                Field(overrides.hasDamageThrust);
                Field(overrides.hasAiFight);
                Field(overrides.hasAiFlee);
                Field(overrides.hasAiAlarm);
                Field(overrides.hasAiServices);
                Field(overrides.hasFlags);
                Field(overrides.hasScript);
                Field(overrides.hasInventory);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::DOOR)
    {
        for (auto &&record : worldstate->doorRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mOpenSound, true);
            Field(recordData.mCloseSound, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasOpenSound);
                Field(overrides.hasCloseSound);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::GAMESETTING)
    {
        for (auto&& record : worldstate->gameSettingRecords)
        {
            auto& recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(record.variable.variableType, true);

            short variableType = record.variable.variableType;

            if (variableType == mwmp::VARIABLE_TYPE::INT)
            {
                Field(record.variable.intValue);
                recordData.mValue.setType(ESM::VarType::VT_Int);
                recordData.mValue.setInteger(record.variable.intValue);
            }
            else if (variableType == mwmp::VARIABLE_TYPE::FLOAT)
            {
                Field(record.variable.floatValue);

                if (variableType == mwmp::VARIABLE_TYPE::FLOAT)
                    recordData.mValue.setType(ESM::VarType::VT_Float);

                recordData.mValue.setFloat(record.variable.floatValue);
            }
            else if (variableType == mwmp::VARIABLE_TYPE::STRING)
            {
                Field(record.variable.stringValue, true);
                recordData.mValue.setType(ESM::VarType::VT_String);
                recordData.mValue.setString(record.variable.stringValue);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::INGREDIENT)
    {
        for (auto &&record : worldstate->ingredientRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mWeight);
            Field(recordData.mData.mValue);
            Field(recordData.mData.mEffectID);
            Field(recordData.mData.mAttributes);
            Field(recordData.mData.mSkills);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasEffects);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::LIGHT)
    {
        for (auto &&record : worldstate->lightRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mSound, true);
            Field(recordData.mData.mWeight, true);
            Field(recordData.mData.mValue, true);
            Field(recordData.mData.mTime, true);
            Field(recordData.mData.mRadius, true);
            Field(recordData.mData.mColor, true);
            Field(recordData.mData.mFlags, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasSound);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasTime);
                Field(overrides.hasRadius);
                Field(overrides.hasColor);
                Field(overrides.hasFlags);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::LOCKPICK)
    {
        for (auto &&record : worldstate->lockpickRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mWeight, true);
            Field(recordData.mData.mValue, true);
            Field(recordData.mData.mQuality, true);
            Field(recordData.mData.mUses, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasQuality);
                Field(overrides.hasUses);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::NPC)
    {
        for (auto &&record : worldstate->npcRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(record.inventoryBaseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mFlags);
            Field(recordData.mRace, true);
            Field(recordData.mModel, true);
            Field(recordData.mHair, true);
            Field(recordData.mHead, true);
            Field(recordData.mClass, true);
            Field(recordData.mFaction, true);
            Field(recordData.mScript, true);
            Field(recordData.mNpdt.mLevel);
            Field(recordData.mNpdt.mHealth);
            Field(recordData.mNpdt.mMana);
            Field(recordData.mNpdt.mFatigue);
            Field(recordData.mAiData.mFight);
            Field(recordData.mAiData.mFlee);
            Field(recordData.mAiData.mAlarm);
            Field(recordData.mAiData.mServices);

            Field(recordData.mNpdtType);
            ProcessInventoryList(record.inventory, recordData.mInventory, send);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasGender);
                Field(overrides.hasFlags);
                Field(overrides.hasRace);
                Field(overrides.hasModel);
                Field(overrides.hasHair);
                Field(overrides.hasHead);
                Field(overrides.hasFaction);
                Field(overrides.hasScript);
                Field(overrides.hasLevel);
                Field(overrides.hasHealth);
                Field(overrides.hasMagicka);
                Field(overrides.hasFatigue);
                Field(overrides.hasAiFight);
                Field(overrides.hasAiFlee);
                Field(overrides.hasAiAlarm);
                Field(overrides.hasAiServices);
                Field(overrides.hasAutoCalc);
                Field(overrides.hasInventory);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::PROBE)
    {
        for (auto &&record : worldstate->probeRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mWeight, true);
            Field(recordData.mData.mValue, true);
            Field(recordData.mData.mQuality, true);
            Field(recordData.mData.mUses, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasQuality);
                Field(overrides.hasUses);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::REPAIR)
    {
        for (auto &&record : worldstate->repairRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mName, true);
            Field(recordData.mModel, true);
            Field(recordData.mIcon, true);
            Field(recordData.mData.mWeight, true);
            Field(recordData.mData.mValue, true);
            Field(recordData.mData.mQuality, true);
            Field(recordData.mData.mUses, true);
            Field(recordData.mScript, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasName);
                Field(overrides.hasModel);
                Field(overrides.hasIcon);
                Field(overrides.hasWeight);
                Field(overrides.hasValue);
                Field(overrides.hasQuality);
                Field(overrides.hasUses);
                Field(overrides.hasScript);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::SCRIPT)
    {
        for (auto &&record : worldstate->scriptRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mScriptText, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasScriptText);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::STATIC)
    {
        for (auto &&record : worldstate->staticRecords)
        {
            auto &recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mModel, true);

            if (!record.baseId.empty())
            {
                auto &&overrides = record.baseOverrides;
                Field(overrides.hasModel);
            }
        }
    }
    else if (worldstate->recordsType == mwmp::RECORD_TYPE::SOUND)
    {
        for (auto&& record : worldstate->soundRecords)
        {
            auto& recordData = record.data;

            Field(record.baseId, true);
            Field(recordData.mId, true);
            Field(recordData.mSound, true);
            Field(recordData.mData.mVolume);
            Field(recordData.mData.mMinRange);
            Field(recordData.mData.mMaxRange);

            if (!record.baseId.empty())
            {
                auto&& overrides = record.baseOverrides;
                Field(overrides.hasSound);
                Field(overrides.hasVolume);
                Field(overrides.hasMinRange);
                Field(overrides.hasMaxRange);
            }
        }
    }
}

void PacketRecordDynamic::ProcessEffects(ESM::EffectList &effectList, bool send)
{
    uint32_t effectCount = 0;

    if (send)
        effectCount = static_cast<uint32_t>(effectList.mList.size());

    if (!CollectionSize(effectCount, protocol::limits::spellEffects))
        return;

    if (!send)
    {
        effectList.mList.clear();
        effectList.mList.resize(effectCount);
    }

    for (auto &&effect : effectList.mList)
    {
        Field(effect.mData.mEffectID);
        Field(effect.mData.mAttribute);
        Field(effect.mData.mSkill);
        Field(effect.mData.mRange);
        Field(effect.mData.mArea);
        Field(effect.mData.mDuration);
        Field(effect.mData.mMagnMax);
        Field(effect.mData.mMagnMin);
    }
}

void PacketRecordDynamic::ProcessBodyParts(ESM::PartReferenceList &partList, bool send)
{
    uint32_t partCount = 0;

    if (send)
        partCount = static_cast<uint32_t>(partList.mParts.size());

    if (!CollectionSize(partCount, maxParts))
        return;

    if (!send)
    {
        partList.mParts.clear();
        partList.mParts.resize(partCount);
    }

    for (auto &&part : partList.mParts)
    {
        Field(part.mPart);
        Field(part.mMale, true);
        Field(part.mFemale, true);
    }
}

// ESM::InventoryList has a strange structure that makes it hard to read in packets directly, so we just deal with it
// here with the help of a separate mwmp::Item vector
void PacketRecordDynamic::ProcessInventoryList(std::vector<mwmp::Item> &inventory, ESM::InventoryList &inventoryList, bool send)
{
    uint32_t itemCount = 0;

    if (send)
        itemCount = static_cast<uint32_t>(inventory.size());

    if (!CollectionSize(itemCount, maxItems))
        return;

    if (!send)
    {
        inventory.clear();
        inventory.resize(itemCount);
        inventoryList.mList.clear();
    }

    for (auto &&item : inventory)
    {
        Field(item.refId, true);
        Field(item.count, true);

        if (!send)
        {
            ESM::ContItem contItem;
            contItem.mItem = ESM::RefId::stringRefId(item.refId);
            contItem.mCount = item.count;
            inventoryList.mList.push_back(contItem);
        }
    }
}
