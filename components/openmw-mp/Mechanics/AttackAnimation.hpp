#ifndef OPENMW_MP_ATTACKANIMATION_HPP
#define OPENMW_MP_ATTACKANIMATION_HPP

#include <cstdint>

namespace mwmp::mechanics
{
    // Targetless release/cancellation packets carry animation, never damage.
    constexpr bool isAttackAnimationOnly(bool pressed, bool isHit, bool targetIsPlayer,
        std::uint64_t targetGuid, unsigned int targetRefNum, unsigned int targetMpNum) noexcept
    {
        return pressed || (!isHit && !targetIsPlayer && targetGuid == 0
            && targetRefNum == 0 && targetMpNum == 0);
    }
    template <class Attack>
    bool isAttackAnimationOnly(const Attack& attack) noexcept
    {
        return isAttackAnimationOnly(attack.pressed, attack.isHit, attack.target.isPlayer,
            attack.target.guid.value, attack.target.refNum, attack.target.mpNum);
    }
}

#endif
