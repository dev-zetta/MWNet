#ifndef OPENMW_MP_NETWORK_ACTIVE_SPELL_HPP
#define OPENMW_MP_NETWORK_ACTIVE_SPELL_HPP

#include <components/esm3/activespells.hpp>
#include <components/esm3/loadmgef.hpp>

#include <utility>

namespace mwmp
{
    // The wire format carries resolved magnitudes, not the engine's random-roll
    // bounds or spellbook lifetime flags. Reconstruct a temporary received cast.
    inline ESM::ActiveSpells::ActiveSpellParams networkActiveSpell(
        const ESM::RefId& id, bool stack, std::vector<ESM::ActiveEffect> effects,
        const std::string& displayName, ESM::RefNum caster)
    {
        ESM::ActiveSpells::ActiveSpellParams result{};
        result.mSourceSpellId = id;
        result.mDisplayName = displayName;
        result.mCaster = caster;
        result.mWorsenings = -1;
        result.mFlags = static_cast<ESM::ActiveSpells::Flags>(
            ESM::ActiveSpells::Flag_Temporary | (stack ? ESM::ActiveSpells::Flag_Stackable : 0));
        for (std::size_t i = 0; i < effects.size(); ++i)
        {
            auto& effect = effects[i];
            effect.mMinMagnitude = effect.mMagnitude;
            effect.mMaxMagnitude = effect.mMagnitude;
            effect.mEffectIndex = static_cast<int32_t>(i);
            effect.mFlags = ESM::ActiveEffect::Flag_Ignore_Resistances
                | ESM::ActiveEffect::Flag_Ignore_Reflect
                | ESM::ActiveEffect::Flag_Ignore_SpellAbsorption;
        }
        result.mEffects = std::move(effects);
        return result;
    }

    // These resources are already advanced by the server's ActiveEffectLedger.
    // Clients still present the effect, but must not apply a second stat tick.
    inline bool serverTicksMagicEffect(const ESM::RefId& id)
    {
        return id == ESM::MagicEffect::DamageHealth || id == ESM::MagicEffect::RestoreHealth
            || id == ESM::MagicEffect::FireDamage || id == ESM::MagicEffect::FrostDamage
            || id == ESM::MagicEffect::ShockDamage || id == ESM::MagicEffect::Poison
            || id == ESM::MagicEffect::SunDamage || id == ESM::MagicEffect::AbsorbHealth
            || id == ESM::MagicEffect::DamageMagicka || id == ESM::MagicEffect::RestoreMagicka
            || id == ESM::MagicEffect::AbsorbMagicka || id == ESM::MagicEffect::DamageFatigue
            || id == ESM::MagicEffect::RestoreFatigue || id == ESM::MagicEffect::AbsorbFatigue;
    }
}

#endif
