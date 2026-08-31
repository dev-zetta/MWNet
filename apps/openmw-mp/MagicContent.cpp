#include "MagicContent.hpp"

#include <components/esm/defs.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esmloader/esmdata.hpp>
#include <components/esmloader/lessbyid.hpp>
#include <components/esmloader/load.hpp>
#include <components/files/collections.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/toutf8/toutf8.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string_view>

namespace mwmp
{
    namespace
    {
        template <class T>
        const T* findRecord(const std::vector<T>& records,
            const ESM::RefId& id)
        {
            const auto it = std::lower_bound(
                records.begin(), records.end(), id, EsmLoader::LessById{});
            return it == records.end() || it->mId != id ? nullptr : &*it;
        }

        std::string canonicalId(const ESM::RefId& id)
        {
            if (id.empty())
                return {};
            return Misc::StringUtils::lowerCase(id.getRefIdString());
        }

        mechanics::SpellRange spellRange(int range)
        {
            switch (range)
            {
                case ESM::RT_Self: return mechanics::SpellRange::Self;
                case ESM::RT_Touch: return mechanics::SpellRange::Touch;
                case ESM::RT_Target: return mechanics::SpellRange::Target;
                default: throw std::runtime_error("magic effect has an invalid range");
            }
        }

        double rangeLimit(mechanics::SpellRange range,
            const MagicContentOptions& options)
        {
            switch (range)
            {
                case mechanics::SpellRange::Self: return 0;
                case mechanics::SpellRange::Touch: return options.maximumTouchRange;
                case mechanics::SpellRange::Target: return options.maximumTargetRange;
            }
            return 0;
        }

        double effectCost(const ESM::ENAMstruct& effect,
            const ESM::MagicEffect& magicEffect, double effectCostMultiplier)
        {
            const bool hasMagnitude
                = (magicEffect.mData.mFlags & ESM::MagicEffect::NoMagnitude) == 0;
            const bool hasDuration
                = (magicEffect.mData.mFlags & ESM::MagicEffect::NoDuration) == 0;
            const bool appliedOnce
                = (magicEffect.mData.mFlags & ESM::MagicEffect::AppliedOnce) != 0;
            const int minimumMagnitude
                = hasMagnitude ? std::max(1, effect.mMagnMin) : 1;
            const int maximumMagnitude
                = hasMagnitude ? std::max(1, effect.mMagnMax) : 1;
            int duration = hasDuration ? effect.mDuration : 1;
            if (!appliedOnce)
                duration = std::max(1, duration);

            double result = 0.5 * (minimumMagnitude + maximumMagnitude);
            result *= 0.1 * magicEffect.mData.mBaseCost;
            result *= duration;
            result += 0.05 * std::max(0, effect.mArea)
                * magicEffect.mData.mBaseCost;
            result *= effectCostMultiplier;
            if (effect.mRange == ESM::RT_Target)
                result *= 1.5;
            return std::max(0.0, result);
        }

        double castingDifficulty(const ESM::ENAMstruct& effect,
            const ESM::MagicEffect& magicEffect, double effectCostMultiplier)
        {
            double result = effect.mDuration;
            if ((magicEffect.mData.mFlags & ESM::MagicEffect::AppliedOnce) == 0)
                result = std::max(1.0, result);
            result *= 0.1 * magicEffect.mData.mBaseCost;
            result *= 0.5 * (effect.mMagnMin + effect.mMagnMax);
            result += effect.mArea * 0.05 * magicEffect.mData.mBaseCost;
            if (effect.mRange == ESM::RT_Target)
                result *= 1.5;
            return std::max(0.0, result * effectCostMultiplier);
        }

        double valueOrZero(const std::map<ESM::RefId, double>& values,
            const ESM::RefId& id)
        {
            const auto found = values.find(id);
            return found == values.end() ? 0.0 : found->second;
        }

        double roundEven(double value)
        {
            const double floorValue = std::floor(value);
            const double fraction = value - floorValue;
            if (fraction < 0.5)
                return floorValue;
            if (fraction > 0.5)
                return floorValue + 1;
            return std::fmod(floorValue, 2.0) == 0
                ? floorValue : floorValue + 1;
        }

        void addSpells(const ESM::SpellList& source,
            mechanics::ActorMagicTemplate& actor)
        {
            for (const ESM::RefId& id : source.mList)
            {
                const std::string value = canonicalId(id);
                if (!value.empty())
                    actor.spells.emplace(value);
            }
        }

        void addInventory(const ESM::InventoryList& source,
            mechanics::ActorMagicTemplate& actor)
        {
            for (const ESM::ContItem& item : source.mList)
            {
                const std::string id = canonicalId(item.mItem);
                if (id.empty() || item.mCount <= 0)
                    continue;
                actor.inventory.push_back({ id, {}, -1, -1, item.mCount });
            }
        }

        std::map<ESM::RefId, double> autoNpcAttributes(const ESM::NPC& npc,
            const ESM::Race& race, const ESM::Class& characterClass,
            const std::vector<ESM::Skill>& skills)
        {
            std::map<ESM::RefId, double> result;
            const bool male = (npc.mFlags & ESM::NPC::Female) == 0;
            for (int index = 0; index < ESM::Attribute::Length; ++index)
            {
                const ESM::RefId id = ESM::Attribute::indexToRefId(index);
                result.emplace(id, race.mData.getAttribute(id, male));
            }
            for (const ESM::RefId& id : characterClass.mData.mAttribute)
            {
                if (!id.empty())
                    result[id] += 10;
            }
            for (auto& [attribute, value] : result)
            {
                double multiplier = 0;
                for (const ESM::Skill& skill : skills)
                {
                    if (skill.mData.mAttribute != attribute)
                        continue;
                    double addition = 0.2;
                    for (const auto& classSkills : characterClass.mData.mSkills)
                    {
                        if (classSkills[0] == skill.mId)
                            addition = 0.5;
                        if (classSkills[1] == skill.mId)
                            addition = 1.0;
                    }
                    multiplier += addition;
                }
                value = std::min(100.0, roundEven(value
                    + (static_cast<double>(npc.mNpdt.mLevel) - 1.0)
                        * multiplier));
            }
            return result;
        }

        std::map<ESM::RefId, double> autoNpcSkills(const ESM::NPC& npc,
            const ESM::Race& race, const ESM::Class& characterClass,
            const std::vector<ESM::Skill>& skills)
        {
            std::map<ESM::RefId, double> result;
            for (const ESM::Skill& skill : skills)
            {
                double value = 0;
                for (int column = 0; column < 2; ++column)
                {
                    for (const auto& classSkills : characterClass.mData.mSkills)
                    {
                        if (classSkills[column] == skill.mId)
                            value += column == 0 ? 10 : 25;
                    }
                }
                const auto raceBonus = std::find_if(race.mData.mBonus.begin(),
                    race.mData.mBonus.end(), [&skill](const auto& bonus) {
                        return bonus.mSkill == skill.mId;
                    });
                if (raceBonus != race.mData.mBonus.end())
                    value += raceBonus->mBonus;
                value += 5;

                double majorityMultiplier = 0.1;
                for (const auto& classSkills : characterClass.mData.mSkills)
                {
                    if (std::find(classSkills.begin(), classSkills.end(), skill.mId)
                        != classSkills.end())
                    {
                        majorityMultiplier = 1.0;
                        break;
                    }
                }
                double specializationMultiplier = 0;
                if (skill.mData.mSpecialization
                    == characterClass.mData.mSpecialization)
                {
                    specializationMultiplier = 0.5;
                    value += 5;
                }
                value += (static_cast<double>(npc.mNpdt.mLevel) - 1.0)
                    * (majorityMultiplier + specializationMultiplier);
                result.emplace(skill.mId,
                    std::min(100.0, roundEven(value)));
            }
            return result;
        }

        void addMagicSkills(const std::map<ESM::RefId, double>& skills,
            mechanics::ActorMagicTemplate& actor)
        {
            for (int index = 0; index < ESM::MagicSchool::Length; ++index)
            {
                const ESM::RefId id = ESM::MagicSchool::indexToSkillRefId(index);
                actor.magicSkills.emplace(canonicalId(id), valueOrZero(skills, id));
            }
            actor.enchantSkill = valueOrZero(skills, ESM::Skill::Enchant);
        }

        std::vector<mechanics::ActorMagicTemplate> makeActorTemplates(
            const EsmLoader::EsmData& data, double npcMagickaMultiplier)
        {
            std::vector<mechanics::ActorMagicTemplate> result;
            result.reserve(data.mNpcs.size() + data.mCreatures.size());
            for (const ESM::NPC& npc : data.mNpcs)
            {
                mechanics::ActorMagicTemplate actor;
                actor.refId = canonicalId(npc.mId);
                std::map<ESM::RefId, double> attributes;
                std::map<ESM::RefId, double> skills;
                const ESM::Race* race = findRecord(data.mRaces, npc.mRace);
                if (npc.mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS)
                {
                    const ESM::Class* characterClass
                        = findRecord(data.mClasses, npc.mClass);
                    if (race == nullptr || characterClass == nullptr)
                        continue;
                    attributes = autoNpcAttributes(
                        npc, *race, *characterClass, data.mSkills);
                    skills = autoNpcSkills(npc, *race, *characterClass, data.mSkills);
                    const double strength = valueOrZero(
                        attributes, ESM::Attribute::Strength);
                    const double endurance = valueOrZero(
                        attributes, ESM::Attribute::Endurance);
                    int healthMultiplier = 3;
                    if (characterClass->mData.mSpecialization == ESM::Class::Combat)
                        healthMultiplier += 2;
                    else if (characterClass->mData.mSpecialization == ESM::Class::Stealth)
                        healthMultiplier += 1;
                    if (std::find(characterClass->mData.mAttribute.begin(),
                            characterClass->mData.mAttribute.end(),
                            ESM::Attribute::Endurance)
                        != characterClass->mData.mAttribute.end())
                    {
                        ++healthMultiplier;
                    }
                    actor.maximumHealth = std::floor(0.5 * (strength + endurance))
                        + healthMultiplier * (npc.mNpdt.mLevel - 1);
                    actor.maximumMagicka = npcMagickaMultiplier
                        * valueOrZero(attributes, ESM::Attribute::Intelligence);
                    actor.maximumFatigue = strength + endurance
                        + valueOrZero(attributes, ESM::Attribute::Agility)
                        + valueOrZero(attributes, ESM::Attribute::Willpower);
                }
                else
                {
                    for (const auto& [id, value] : npc.mNpdt.mAttributes)
                        attributes.emplace(id, value);
                    for (const auto& [id, value] : npc.mNpdt.mSkills)
                        skills.emplace(id, value);
                    actor.maximumHealth = npc.mNpdt.mHealth;
                    actor.maximumMagicka = npc.mNpdt.mMana;
                    actor.maximumFatigue = npc.mNpdt.mFatigue;
                }
                actor.willpower = valueOrZero(
                    attributes, ESM::Attribute::Willpower);
                actor.luck = valueOrZero(attributes, ESM::Attribute::Luck);
                addMagicSkills(skills, actor);
                addSpells(npc.mSpells, actor);
                if (race != nullptr)
                    addSpells(race->mPowers, actor);
                addInventory(npc.mInventory, actor);
                result.push_back(std::move(actor));
            }

            for (const ESM::Creature& creature : data.mCreatures)
            {
                mechanics::ActorMagicTemplate actor;
                actor.refId = canonicalId(creature.mId);
                actor.maximumHealth = std::max(0, creature.mData.mHealth);
                actor.maximumMagicka = std::max(0, creature.mData.mMana);
                actor.maximumFatigue = std::max(0, creature.mData.mFatigue);
                actor.willpower = std::max(0,
                    creature.mData.getAttribute(ESM::Attribute::Willpower));
                actor.luck = std::max(0,
                    creature.mData.getAttribute(ESM::Attribute::Luck));
                actor.enchantSkill = std::max(0, creature.mData.mMagic);
                for (int index = 0; index < ESM::MagicSchool::Length; ++index)
                {
                    actor.magicSkills.emplace(canonicalId(
                        ESM::MagicSchool::indexToSkillRefId(index)),
                        std::max(0, creature.mData.mMagic));
                }
                addSpells(creature.mSpells, actor);
                addInventory(creature.mInventory, actor);
                result.push_back(std::move(actor));
            }
            return result;
        }

        mechanics::SpellEffectDefinition makeEffect(
            const ESM::ENAMstruct& effect, const ESM::MagicEffect& magicEffect,
            double effectCostMultiplier, const MagicContentOptions& options)
        {
            const bool hasMagnitude
                = (magicEffect.mData.mFlags & ESM::MagicEffect::NoMagnitude) == 0;
            const bool hasDuration
                = (magicEffect.mData.mFlags & ESM::MagicEffect::NoDuration) == 0;
            const bool appliedOnce
                = (magicEffect.mData.mFlags & ESM::MagicEffect::AppliedOnce) != 0;
            const mechanics::SpellRange range = spellRange(effect.mRange);

            mechanics::SpellEffectKind kind = mechanics::SpellEffectKind::Timed;
            if (appliedOnce || !hasDuration)
                kind = mechanics::SpellEffectKind::Instant;
            if (kind == mechanics::SpellEffectKind::Instant)
            {
                if (magicEffect.mId == ESM::MagicEffect::DamageHealth
                    || magicEffect.mId == ESM::MagicEffect::FireDamage
                    || magicEffect.mId == ESM::MagicEffect::FrostDamage
                    || magicEffect.mId == ESM::MagicEffect::ShockDamage
                    || magicEffect.mId == ESM::MagicEffect::Poison
                    || magicEffect.mId == ESM::MagicEffect::SunDamage)
                    kind = mechanics::SpellEffectKind::DamageHealth;
                else if (magicEffect.mId == ESM::MagicEffect::RestoreHealth)
                    kind = mechanics::SpellEffectKind::RestoreHealth;
                else if (magicEffect.mId == ESM::MagicEffect::DamageMagicka)
                    kind = mechanics::SpellEffectKind::DamageMagicka;
                else if (magicEffect.mId == ESM::MagicEffect::RestoreMagicka)
                    kind = mechanics::SpellEffectKind::RestoreMagicka;
                else if (magicEffect.mId == ESM::MagicEffect::DamageFatigue)
                    kind = mechanics::SpellEffectKind::DamageFatigue;
                else if (magicEffect.mId == ESM::MagicEffect::RestoreFatigue)
                    kind = mechanics::SpellEffectKind::RestoreFatigue;
                else if (magicEffect.mId == ESM::MagicEffect::AbsorbHealth)
                    kind = mechanics::SpellEffectKind::AbsorbHealth;
                else if (magicEffect.mId == ESM::MagicEffect::AbsorbMagicka)
                    kind = mechanics::SpellEffectKind::AbsorbMagicka;
                else if (magicEffect.mId == ESM::MagicEffect::AbsorbFatigue)
                    kind = mechanics::SpellEffectKind::AbsorbFatigue;
            }

            std::string argument;
            if (!effect.mAttribute.empty())
                argument = canonicalId(effect.mAttribute);
            else if (!effect.mSkill.empty())
                argument = canonicalId(effect.mSkill);

            return mechanics::SpellEffectDefinition(
                canonicalId(magicEffect.mId), std::move(argument), kind, range,
                hasMagnitude ? effect.mMagnMin : 1,
                hasMagnitude ? effect.mMagnMax : 1,
                kind == mechanics::SpellEffectKind::Timed
                    ? std::max(1, effect.mDuration) : 0,
                rangeLimit(range, options), canonicalId(magicEffect.mData.mSchool),
                castingDifficulty(effect, magicEffect, effectCostMultiplier));
        }

        template <class Record>
        std::vector<mechanics::SpellEffectDefinition> makeEffects(
            const Record& record, const EsmLoader::EsmData& data,
            double effectCostMultiplier, const MagicContentOptions& options)
        {
            std::vector<mechanics::SpellEffectDefinition> result;
            result.reserve(record.mEffects.mList.size());
            for (const ESM::IndexedENAMstruct& effect : record.mEffects.mList)
            {
                const ESM::MagicEffect* magicEffect
                    = findRecord(data.mMagicEffects, effect.mData.mEffectID);
                if (magicEffect == nullptr)
                    throw std::runtime_error("magic content references an unknown effect");
                result.push_back(makeEffect(
                    effect.mData, *magicEffect, effectCostMultiplier, options));
            }
            return result;
        }

        template <class Record>
        double automaticCost(const Record& record,
            const EsmLoader::EsmData& data, double effectCostMultiplier)
        {
            double result = 0;
            for (const ESM::IndexedENAMstruct& effect : record.mEffects.mList)
            {
                const ESM::MagicEffect* magicEffect
                    = findRecord(data.mMagicEffects, effect.mData.mEffectID);
                if (magicEffect == nullptr)
                    throw std::runtime_error("magic content references an unknown effect");
                result += effectCost(
                    effect.mData, *magicEffect, effectCostMultiplier);
            }
            return std::round(result);
        }
    }

    CanonicalMagicContent loadCanonicalMagicContent(
        const MagicContentOptions& options)
    {
        if (options.dataDirectories.empty() || options.contentFiles.empty())
            throw std::runtime_error("canonical magic content paths are empty");
        if (!std::isfinite(options.maximumTouchRange)
            || options.maximumTouchRange <= 0
            || !std::isfinite(options.maximumTargetRange)
            || options.maximumTargetRange <= 0)
        {
            throw std::runtime_error("canonical magic ranges must be positive");
        }

        const Files::Collections collections(options.dataDirectories);
        ESM::ReadersCache readers(options.contentFiles.size());
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(options.encoding));
        EsmLoader::Query query;
        query.mLoadGameSettings = true;
        query.mLoadActorMagic = true;
        query.mLoadMagic = true;
        EsmLoader::EsmData data = EsmLoader::loadEsmData(query,
            options.contentFiles, collections, readers, &encoder);
        const double effectCostMultiplier
            = EsmLoader::getGameSetting(data.mGameSettings,
                "fEffectCostMult").getFloat();
        if (!std::isfinite(effectCostMultiplier) || effectCostMultiplier < 0)
            throw std::runtime_error("fEffectCostMult is invalid");
        const double npcMagickaMultiplier = EsmLoader::getGameSetting(
            data.mGameSettings, "fNPCbaseMagickaMult").getFloat();
        if (!std::isfinite(npcMagickaMultiplier) || npcMagickaMultiplier < 0)
            throw std::runtime_error("fNPCbaseMagickaMult is invalid");

        CanonicalMagicContent result;
        result.fatigueBase = EsmLoader::getGameSetting(
            data.mGameSettings, "fFatigueBase").getFloat();
        result.fatigueMultiplier = EsmLoader::getGameSetting(
            data.mGameSettings, "fFatigueMult").getFloat();
        if (!std::isfinite(result.fatigueBase)
            || !std::isfinite(result.fatigueMultiplier))
        {
            throw std::runtime_error("canonical fatigue settings are invalid");
        }
        result.actorTemplates = makeActorTemplates(data, npcMagickaMultiplier);
        result.definitions.reserve(data.mSpells.size()
            + data.mEnchantedItems.size());
        for (const ESM::Spell& spell : data.mSpells)
        {
            if (spell.mData.mType != ESM::Spell::ST_Spell
                && spell.mData.mType != ESM::Spell::ST_Power)
                continue;
            mechanics::SpellDefinition definition;
            definition.id = canonicalId(spell.mId);
            definition.displayName = spell.mName.empty()
                ? definition.id : spell.mName;
            definition.magickaCost
                = (spell.mData.mFlags & ESM::Spell::F_Autocalc) != 0
                ? automaticCost(spell, data, effectCostMultiplier)
                : std::max(0, spell.mData.mCost);
            definition.alwaysSucceeds
                = (spell.mData.mFlags & ESM::Spell::F_Always) != 0;
            definition.effects
                = makeEffects(spell, data, effectCostMultiplier, options);
            if (!definition.effects.empty())
                result.definitions.push_back(std::move(definition));
        }

        for (const EsmLoader::EnchantedItem& item : data.mEnchantedItems)
        {
            const ESM::Enchantment* enchantment
                = findRecord(data.mEnchantments, item.mEnchantment);
            if (enchantment == nullptr
                || (enchantment->mData.mType != ESM::Enchantment::CastOnce
                    && enchantment->mData.mType != ESM::Enchantment::WhenUsed))
                continue;
            mechanics::SpellDefinition definition;
            definition.id = canonicalId(item.mId);
            definition.displayName = definition.id;
            definition.sourceKind = mechanics::SpellSourceKind::Item;
            definition.itemChargeCost = item.mConsumable ? 0
                : ((enchantment->mData.mFlags & ESM::Enchantment::Autocalc) != 0
                    ? automaticCost(*enchantment, data, effectCostMultiplier)
                    : std::max(0, enchantment->mData.mCost));
            definition.itemMaximumCharge = item.mConsumable ? 0
                : std::max(0, enchantment->mData.mCharge);
            definition.alwaysSucceeds = true;
            definition.effects = makeEffects(
                *enchantment, data, effectCostMultiplier, options);
            if (definition.effects.empty())
                continue;
            if (item.mConsumable)
                result.consumableItems.insert(definition.id);
            result.definitions.push_back(std::move(definition));
        }
        return result;
    }
}
