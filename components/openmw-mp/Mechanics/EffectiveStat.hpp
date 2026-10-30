#ifndef OPENMW_MP_MECHANICS_EFFECTIVE_STAT_HPP
#define OPENMW_MP_MECHANICS_EFFECTIVE_STAT_HPP

#include "CombatResolver.hpp"

#include <components/esm3/statstate.hpp>

#include <algorithm>
#include <cmath>

namespace mwmp::mechanics
{
    // Attribute and skill snapshots carry base, modifier and damage. Their
    // mCurrent field belongs to dynamic resources and does not describe them.
    inline double effectiveStatValue(const ESM::StatState<float>& stat) noexcept
    {
        const double value = static_cast<double>(stat.mBase) + stat.mMod - stat.mDamage;
        return std::isfinite(value)
            ? std::clamp(value, 0.0, CombatResolver::MaximumStatValue) : 0;
    }

    // DynamicStat::readState interprets mMod as an additive modifier. Current
    // may be depleted, negative fatigue, or stale; it never sets capacity.
    // Keep the server's positive placeholder for an uninitialized maximum.
    inline double dynamicStatMaximum(const ESM::StatState<float>& stat) noexcept
    {
        const double value = static_cast<double>(stat.mBase) + stat.mMod;
        return std::isfinite(value)
            ? std::clamp(value, 1.0, CombatResolver::MaximumStatValue) : 1;
    }

    // Callers provide already validated canonical resources. Store maximum as
    // base with no additive modifier, and retain depleted/negative current.
    inline void setCanonicalDynamicStat(
        ESM::StatState<float>& stat, double maximum, double current) noexcept
    {
        stat.mBase = static_cast<float>(maximum);
        stat.mMod = 0;
        stat.mCurrent = static_cast<float>(current);
        stat.mDamage = 0;
        stat.mProgress = 0;
    }
}

#endif
