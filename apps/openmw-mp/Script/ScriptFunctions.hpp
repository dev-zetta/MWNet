#ifndef SCRIPTFUNCTIONS_HPP
#define SCRIPTFUNCTIONS_HPP

#include <Script/Functions/Actors.hpp>
#include <Script/Functions/Books.hpp>
#include <Script/Functions/Cells.hpp>
#include <Script/Functions/CharClass.hpp>
#include <Script/Functions/Chat.hpp>
#include <Script/Functions/Dialogue.hpp>
#include <Script/Functions/Factions.hpp>
#include <Script/Functions/GUI.hpp>
#include <Script/Functions/Items.hpp>
#include <Script/Functions/Mechanics.hpp>
#include <Script/Functions/Miscellaneous.hpp>
#include <Script/Functions/Objects.hpp>
#include <Script/Functions/Positions.hpp>
#include <Script/Functions/Quests.hpp>
#include <Script/Functions/RecordsDynamic.hpp>
#include <Script/Functions/Shapeshift.hpp>
#include <Script/Functions/Server.hpp>
#include <Script/Functions/Settings.hpp>
#include <Script/Functions/Spells.hpp>
#include <Script/Functions/Stats.hpp>
#include <Script/Functions/Worldstate.hpp>
#include <stdexcept>
#include <string>
#include <tuple>
#include <apps/openmw-mp/Player.hpp>
#include "ScriptFunction.hpp"
#include "Types.hpp"

#include <components/openmw-mp/TimedLog.hpp>

#ifndef __PRETTY_FUNCTION__
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif

#define GET_PLAYER(pid, pl, retvalue) \
     pl = Players::getPlayer(pid); \
     if (pl == nullptr) {\
        throw std::runtime_error(std::string(__PRETTY_FUNCTION__) \
            + ": player with pid " + std::to_string(pid) + " was not found");\
}


class ScriptFunctions
{
public:

    static void MakePublic(ScriptFunc _public, const char *name, char ret_type, const char *def);
    static boost::any CallPublic(const char *name, va_list args);

     /**
     * \brief Create a timer that will run a script function after a certain interval.
     *
     * \param callback The Lua script function.
     * \param msec The interval in miliseconds.
     * \return The ID of the timer thus created.
     */
    static int CreateTimer(ScriptFunc callback, int msec);

    /**
    * \brief Create a timer that will run a script function after a certain interval and pass
    *        certain arguments to it.
    *
    * Example usage:
    * - tes3mp.CreateTimerEx("OnTimerTest1", 250, "i", 90)
    * - tes3mp.CreateTimerEx("OnTimerTest2", 500, "sif", "Test string", 60, 77.321)
    *
    * \param callback The Lua script function.
    * \param msec The interval in miliseconds.
    * \param types The argument types.
    * \param args The arguments.
    * \return The ID of the timer thus created.
    */
    static int CreateTimerEx(ScriptFunc callback, int msec, const char *types, va_list args);

    /**
    * \brief Start the timer with a certain ID.
    *
    * \param timerId The timer ID.
    * \return void
    */
    static void StartTimer(int timerId);

    /**
    * \brief Stop the timer with a certain ID.
    *
    * \param timerId The timer ID.
    * \return void
    */
    static void StopTimer(int timerId);

    /**
    * \brief Restart the timer with a certain ID for a certain interval.
    *
    * \param timerId The timer ID.
    * \param msec The interval in miliseconds.
    * \return void
    */
    static void RestartTimer(int timerId, int msec);

    /**
    * \brief Free the timer with a certain ID.
    *
    * \param timerId The timer ID.
    * \return void
    */
    static void FreeTimer(int timerId);

    /**
    * \brief Check whether a timer is elapsed.
    *
    * \param timerId The timer ID.
    * \return Whether the timer is elapsed.
    */
    static bool IsTimerElapsed(int timerId);


    static constexpr ScriptFunctionMetadata functions[]{
#include "Functions/ScriptFunctions.inc"
    };
    static const ScriptFunctionData runtimeFunctions[sizeof(functions) / sizeof(functions[0])];

    static constexpr ScriptCallbackData callbacks[]{
            {"OnServerInit",             Callback<>()},
            {"OnServerPostInit",         Callback<>()},
            {"OnServerExit",             Callback<bool>()},
            {"OnServerScriptCrash",      Callback<const char*>()},
            {"OnTransportConnect",       Callback<unsigned short>()},
            {"OnPlayerAuthenticated",    Callback<unsigned short, const char*, bool>()},
            {"OnPlayerConnect",          Callback<unsigned short>()},
            {"OnPlayerDisconnect",       Callback<unsigned short>()},
            {"OnPlayerDeath",            Callback<unsigned short>()},
            {"OnPlayerResurrect",        Callback<unsigned short>()},
            {"OnPlayerCellChange",       Callback<unsigned short>()},
            {"OnPlayerCellChangeIntent", Callback<unsigned short, const char*>()},
            {"OnPlayerCellChangeIntentRejected", Callback<unsigned short, const char*, const char*>()},
            {"OnPlayerMovementViolation", Callback<unsigned short, const char*, double, double, unsigned int>()},
            {"OnActorMovementViolation", Callback<unsigned short, const char*, const char*, double, double, unsigned int>()},
            {"OnPlayerAttribute",        Callback<unsigned short>()},
            {"OnPlayerAttributeIntent",  Callback<unsigned short>()},
            {"OnPlayerAttributeIntentRejected", Callback<unsigned short>()},
            {"OnPlayerSkill",            Callback<unsigned short>()},
            {"OnPlayerSkillIntent",      Callback<unsigned short>()},
            {"OnPlayerSkillIntentRejected", Callback<unsigned short>()},
            {"OnPlayerLevel",            Callback<unsigned short>()},
            {"OnPlayerLevelIntent",      Callback<unsigned short>()},
            {"OnPlayerLevelIntentRejected", Callback<unsigned short>()},
            {"OnPlayerBounty",           Callback<unsigned short>()},
            {"OnPlayerBountyIntent",     Callback<unsigned short>()},
            {"OnPlayerBountyIntentRejected", Callback<unsigned short, const char*>()},
            {"OnPlayerJailComplete",     Callback<unsigned short, unsigned long long>()},
            {"OnPlayerReputation",       Callback<unsigned short>()},
            {"OnPlayerEquipmentIntent",  Callback<unsigned short>()},
            {"OnPlayerEquipmentIntentRejected", Callback<unsigned short>()},
            {"OnPlayerEquipment",        Callback<unsigned short>()},
            {"OnPlayerInventoryIntent",  Callback<unsigned short>()},
            {"OnPlayerInventoryIntentRejected", Callback<unsigned short>()},
            {"OnPlayerInventory",        Callback<unsigned short>()},
            {"OnPlayerAttackIntent",     Callback<unsigned short, bool, unsigned short, unsigned int, unsigned int, double>()},
            {"OnPlayerAttackIntentRejected", Callback<unsigned short, const char*>()},
            {"OnPlayerCastIntent",       Callback<unsigned short, bool, bool, unsigned short, unsigned int, unsigned int>()},
            {"OnPlayerCastIntentRejected", Callback<unsigned short, const char*>()},
            {"OnPlayerJournal",          Callback<unsigned short>()},
            {"OnPlayerFaction",          Callback<unsigned short>()},
            {"OnPlayerShapeshift",       Callback<unsigned short>()},
            {"OnPlayerShapeshiftIntent", Callback<unsigned short>()},
            {"OnPlayerShapeshiftIntentRejected", Callback<unsigned short, const char*>()},
            {"OnPlayerSpellbook",        Callback<unsigned short>()},
            {"OnPlayerSpellsActiveIntent", Callback<unsigned short>()},
            {"OnPlayerSpellsActiveIntentRejected", Callback<unsigned short, const char*>()},
            {"OnPlayerSpellsActive",     Callback<unsigned short>()},
            {"OnPlayerCooldowns",        Callback<unsigned short>()},
            {"OnPlayerQuickKeys",        Callback<unsigned short>()},
            {"OnPlayerTopic",            Callback<unsigned short>()},
            {"OnPlayerDisposition",      Callback<unsigned short>()},
            {"OnPlayerBook",             Callback<unsigned short>()},
            {"OnPlayerItemUseIntent",    Callback<unsigned short>()},
            {"OnPlayerItemUseIntentRejected", Callback<unsigned short, const char*>()},
            {"OnPlayerItemUse",          Callback<unsigned short>()},
            {"OnPlayerMiscellaneous",    Callback<unsigned short>()},
            {"OnPlayerInput",            Callback<unsigned short>()},
            {"OnPlayerRest",             Callback<unsigned short>()},
            {"OnRecordDynamic",          Callback<unsigned short>()},
            {"OnCellLoad",               Callback<unsigned short, const char*>()},
            {"OnCellUnload",             Callback<unsigned short, const char*>()},
            {"OnCellDeletion",           Callback<const char*>()},
            {"OnConsoleCommand",         Callback<unsigned short, const char*>()},
            {"OnContainerIntent",        Callback<unsigned short, const char*>()},
            {"OnContainerIntentRejected", Callback<unsigned short, const char*, const char*>()},
            {"OnContainer",              Callback<unsigned short, const char*>()},
            {"OnDoorState",              Callback<unsigned short, const char*>()},
            {"OnObjectActivate",         Callback<unsigned short, const char*>()},
            {"OnObjectHit",              Callback<unsigned short, const char*>()},
            {"OnObjectPlace",            Callback<unsigned short, const char*>()},
            {"OnObjectPlaceIntent",      Callback<unsigned short, const char*>()},
            {"OnObjectPlaceIntentRejected", Callback<unsigned short, const char*, const char*>()},
            {"OnObjectMutationIntent",   Callback<unsigned short, const char*, const char*>()},
            {"OnObjectMutationIntentRejected", Callback<unsigned short, const char*, const char*, const char*>()},
            {"OnObjectMutationCommitted", Callback<unsigned short, const char*, const char*>()},
            {"OnObjectState",            Callback<unsigned short, const char*>()},
            {"OnObjectSpawn",            Callback<unsigned short, const char*>()},
            {"OnObjectDelete",           Callback<unsigned short, const char*>()},
            {"OnObjectLock",             Callback<unsigned short, const char*>()},
            {"OnObjectDialogueChoice",   Callback<unsigned short, const char*>()},
            {"OnObjectMiscellaneous",    Callback<unsigned short, const char*>()},
            {"OnObjectRestock",          Callback<unsigned short, const char*>()},
            {"OnObjectScale",            Callback<unsigned short, const char*>()},
            {"OnObjectSound",            Callback<unsigned short, const char*>()},
            {"OnObjectTrap",             Callback<unsigned short, const char*>()},
            {"OnVideoPlay",              Callback<unsigned short, const char*>()},
            {"OnActorListIntent",        Callback<unsigned short, const char*>()},
            {"OnActorListIntentRejected", Callback<unsigned short, const char*>()},
            {"OnActorRecovered",         Callback<unsigned short, const char*>()},
            {"OnActorList",              Callback<unsigned short, const char*>()},
            {"OnActorEquipmentIntent",   Callback<unsigned short, const char*>()},
            {"OnActorEquipmentIntentRejected", Callback<unsigned short, const char*>()},
            {"OnActorEquipment",         Callback<unsigned short, const char*>()},
            {"OnActorAIIntent",          Callback<unsigned short, const char*>()},
            {"OnActorAIIntentRejected",  Callback<unsigned short, const char*, const char*>()},
            {"OnActorAI",                Callback<unsigned short, const char*>()},
            {"OnActorAttackIntent",      Callback<unsigned short, const char*, unsigned int, bool, unsigned short, unsigned int, unsigned int, double>()},
            {"OnActorAttackIntentRejected", Callback<unsigned short, const char*, unsigned int, const char*>()},
            {"OnActorCastIntent",        Callback<unsigned short, const char*, unsigned int, bool, bool, unsigned short, unsigned int, unsigned int>()},
            {"OnActorCastIntentRejected", Callback<unsigned short, const char*, unsigned int, const char*>()},
            {"OnActorDeath",             Callback<unsigned short, const char*>()},
            {"OnActorSpellsActiveIntent", Callback<unsigned short, const char*>()},
            {"OnActorSpellsActiveIntentRejected", Callback<unsigned short, const char*, const char*>()},
            {"OnActorSpellsActive",      Callback<unsigned short, const char*>()},
            {"OnActorCellChangeIntent",  Callback<unsigned short, const char*>()},
            {"OnActorCellChangeIntentRejected", Callback<unsigned short, const char*, const char*>()},
            {"OnActorCellChange",        Callback<unsigned short, const char*>()},
            {"OnActorTest",              Callback<unsigned short, const char*>()},
            {"OnPlayerSendMessage",      Callback<unsigned short, const char*>()},
            {"OnPlayerEndCharGen",       Callback<unsigned short>()},
            {"OnGUIAction",              Callback<unsigned short, int, const char*>()},
            {"OnWorldKillCount",         Callback<unsigned short>()},
            {"OnWorldMap",               Callback<unsigned short>()},
            {"OnWorldWeather",           Callback<unsigned short>()},
            {"OnClientScriptLocal",      Callback<unsigned short, const char*>()},
            {"OnClientScriptGlobal",     Callback<unsigned short>()},
            {"OnMpNumIncrement",         Callback<int>()},
            {"OnRequestDataFileList",    Callback<>()}
    };
};

#endif //SCRIPTFUNCTIONS_HPP
