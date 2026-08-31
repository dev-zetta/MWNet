#include "MagicContent.hpp"

#include <components/esm/defs.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
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
            if (magicEffect.mId == ESM::MagicEffect::DamageHealth && appliedOnce)
                kind = mechanics::SpellEffectKind::DamageHealth;
            else if (magicEffect.mId == ESM::MagicEffect::RestoreHealth && appliedOnce)
                kind = mechanics::SpellEffectKind::RestoreHealth;

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
        query.mLoadMagic = true;
        EsmLoader::EsmData data = EsmLoader::loadEsmData(query,
            options.contentFiles, collections, readers, &encoder);
        const double effectCostMultiplier
            = EsmLoader::getGameSetting(data.mGameSettings,
                "fEffectCostMult").getFloat();
        if (!std::isfinite(effectCostMultiplier) || effectCostMultiplier < 0)
            throw std::runtime_error("fEffectCostMult is invalid");

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
