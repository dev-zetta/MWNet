#include "ActorMagicRegistry.hpp"

#include <cmath>
#include <utility>

namespace mwmp::mechanics
{
    ActorMagicRegistry::ActorMagicRegistry(std::size_t maximumActors)
        : mMaximumActors(maximumActors)
    {
    }

    bool ActorMagicRegistry::upsert(ActorMagicTemplate actor)
    {
        if (!valid(actor))
            return false;
        const auto found = mActors.find(actor.refId);
        if (found == mActors.end() && mActors.size() >= mMaximumActors)
            return false;
        mActors.insert_or_assign(actor.refId, std::move(actor));
        return true;
    }

    std::optional<ActorMagicTemplate> ActorMagicRegistry::find(
        const std::string& refId) const
    {
        const auto found = mActors.find(refId);
        if (found == mActors.end())
            return std::nullopt;
        return found->second;
    }

    bool ActorMagicRegistry::knowsSpell(
        const std::string& refId, const std::string& spellId) const
    {
        const auto found = mActors.find(refId);
        return found != mActors.end() && found->second.spells.contains(spellId);
    }

    bool ActorMagicRegistry::erase(const std::string& refId) noexcept
    {
        return mActors.erase(refId) != 0;
    }

    void ActorMagicRegistry::clear() noexcept
    {
        mActors.clear();
    }

    std::size_t ActorMagicRegistry::size() const noexcept
    {
        return mActors.size();
    }

    bool ActorMagicRegistry::valid(const ActorMagicTemplate& actor) noexcept
    {
        const auto validStat = [](double value) {
            return std::isfinite(value) && value >= 0 && value <= MaximumStatValue;
        };
        if (!validStat(actor.handToHand) || !validStat(actor.endurance)
            || !validId(actor.refId) || !validStat(actor.maximumHealth)
            || !validStat(actor.maximumMagicka) || !validStat(actor.maximumFatigue)
            || !validStat(actor.willpower) || !validStat(actor.luck)
            || !validStat(actor.enchantSkill)
            || actor.magicSkills.size() > MaximumSkills
            || actor.spells.size() > MaximumSpells
            || actor.inventory.size() > MaximumInventoryStacks)
        {
            return false;
        }
        for (const auto& [skill, value] : actor.magicSkills)
        {
            if (!validId(skill) || !validStat(value))
                return false;
        }
        for (const std::string& spell : actor.spells)
        {
            if (!validId(spell))
                return false;
        }
        for (const InventoryItem& item : actor.inventory)
        {
            if (!validId(item.refId) || !validId(item.soul, true)
                || item.charge < -1 || !std::isfinite(item.enchantmentCharge)
                || item.enchantmentCharge < -1
                || item.enchantmentCharge > InventoryLedger::MaximumEnchantmentCharge
                || item.count <= 0 || item.count > InventoryLedger::MaximumStackCount)
            {
                return false;
            }
        }
        return true;
    }

    bool ActorMagicRegistry::validId(
        const std::string& value, bool allowEmpty) noexcept
    {
        return (allowEmpty || !value.empty()) && value.size() <= MaximumStringBytes;
    }
}
