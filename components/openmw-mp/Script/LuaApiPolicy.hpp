#ifndef OPENMW_MP_SCRIPT_LUA_API_POLICY_HPP
#define OPENMW_MP_SCRIPT_LUA_API_POLICY_HPP

#include <string_view>

namespace mwmp::script
{
    constexpr bool isIntentCallback(std::string_view name) noexcept
    {
        return name.size() > 6 && name.ends_with("Intent");
    }

    constexpr bool isReadOnlyApi(std::string_view name) noexcept
    {
        return name.starts_with("Get") || name.starts_with("Is")
            || name.starts_with("Has") || name.starts_with("Does")
            || name == "LogMessage" || name == "LogAppend"
            || name == "ReadReceivedActorList"
            || name == "ReadReceivedObjectList"
            || name == "ReadReceivedWorldstate"
            // These enforcement/presentation calls do not modify canonical
            // gameplay state and must remain available to validators.
            || name == "Kick" || name == "SendMessage";
    }

    constexpr bool isIntentModifierApi(std::string_view name) noexcept
    {
        return name == "SetPlayerAttackStrength"
            || name == "SetActorAttackStrength"
            || name == "ClearInventoryChanges"
            || name == "SetInventoryChangesAction"
            || name == "AddItemChange"
            || name == "InitializeInventoryChanges"
            || name == "AddItem" || name == "SetBounty";
    }

    constexpr bool intentModifierUsesPlayerId(std::string_view name) noexcept
    {
        return isIntentModifierApi(name) && name != "SetActorAttackStrength";
    }
}

#endif
