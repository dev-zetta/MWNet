#include "Player.hpp"
#include "processors/ProcessorInitializer.hpp"
#include <RakPeer.h>
#include <Kbhit.h>

#include <components/misc/stringops.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Security/AuthenticationMessages.hpp>
#include <components/openmw-mp/Security/PasswordHash.hpp>
#include <components/openmw-mp/Session/SessionState.hpp>
#include <components/openmw-mp/Transport/LegacyPacketFrame.hpp>

#include <sodium.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <Script/Script.hpp>
#include <Script/API/TimerAPI.hpp>
#include <chrono>
#include <thread>
#include <csignal>

#include "Networking.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "processors/PlayerProcessor.hpp"
#include "processors/ActorProcessor.hpp"
#include "processors/ObjectProcessor.hpp"
#include "processors/WorldstateProcessor.hpp"

using namespace mwmp;

Networking *Networking::sThis = 0;

static int currentMpNum = 0;
static bool dataFileEnforcementState = true;
static bool scriptErrorIgnoringState = false;
bool killLoop = false;

Networking::Networking(RakNet::RakPeerInterface *peer,
    transport::Protocol11Endpoint& endpoint,
    const std::filesystem::path& credentialDirectory,
    const std::filesystem::path& legacyPlayerDirectory,
    unsigned int maximumConnections, unsigned short port,
    double movementMaximumSpeed, unsigned int movementViolationLimit)
    : mEndpoint(endpoint)
    , mDispatcher(endpoint.transport(), transport::ApplicationPacketFlow::ServerToClient,
        maximumConnections)
    , mReceiver(transport::ApplicationPacketFlow::ClientToServer)
    , mAuthentication(credentialDirectory, legacyPlayerDirectory)
    , mMovementValidator(maximumConnections)
    , mPlayerLifecycle(maximumConnections)
    , mInventoryLedger(maximumConnections * 2U)
    , mMaximumConnections(maximumConnections)
    , mPort(port)
    , mMovementMaximumSpeed(movementMaximumSpeed)
    , mMovementViolationLimit(movementViolationLimit)
{
    sThis = this;
    this->peer = peer;
    players = Players::getPlayers();

    CellController::create();

    systemPacketController = std::make_unique<SystemPacketController>(peer);
    playerPacketController = std::make_unique<PlayerPacketController>(peer);
    actorPacketController = std::make_unique<ActorPacketController>(peer);
    objectPacketController = std::make_unique<ObjectPacketController>(peer);
    worldstatePacketController = std::make_unique<WorldstatePacketController>(peer);

    // Set send stream
    systemPacketController->SetStream(0, &bsOut);
    playerPacketController->SetStream(0, &bsOut);
    actorPacketController->SetStream(0, &bsOut);
    objectPacketController->SetStream(0, &bsOut);
    worldstatePacketController->SetStream(0, &bsOut);
    systemPacketController->SetApplicationPacketDispatcher(&mDispatcher);
    playerPacketController->SetApplicationPacketDispatcher(&mDispatcher);
    actorPacketController->SetApplicationPacketDispatcher(&mDispatcher);
    objectPacketController->SetApplicationPacketDispatcher(&mDispatcher);
    worldstatePacketController->SetApplicationPacketDispatcher(&mDispatcher);

    running = true;
    exitCode = 0;

    Script::Call<Script::CallbackIdentity("OnServerInit")>();

    ProcessorInitializer();
}

Networking::~Networking()
{
    try
    {
        Script::Call<Script::CallbackIdentity("OnServerExit")>(false);
    }
    catch (const std::exception& exception)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "OnServerExit failed during shutdown: %s", exception.what());
    }
    catch (...)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s",
            "OnServerExit failed during shutdown with an unknown exception");
    }
    mPersistenceService.flush();
    mPersistenceService.stop();

    CellController::destroy();

    sThis = 0;
}

bool Networking::setServerPasswordHash(std::string passwordHash, std::string& error)
{
    return mAuthentication.setAccessPasswordHash(std::move(passwordHash), error);
}

bool Networking::setServerPassword(std::string_view password, std::string& error)
{
    if (password.empty())
        return setServerPasswordHash({}, error);
    auto buffer = security::PasswordBuffer::copyFrom(password, error);
    if (!buffer)
        return false;
    std::string encoded;
    if (!security::PasswordHash::createArgon2id(*buffer, encoded, error))
        return false;
    return setServerPasswordHash(std::move(encoded), error);
}

std::optional<session::AuthorityLease> Networking::assignActorAuthority(
    const ESM::Cell& cell, RakNet::RakNetGUID owner)
{
    if (!mAuthenticatedConnections.contains(owner.g))
        return std::nullopt;

    const std::string cellDescription = cell.getShortDescription();
    const auto now = session::AuthorityLeaseManager::Clock::now();
    if (const auto current = mAuthorityLeases.find(cellDescription); current)
    {
        if (current->owner == owner.g
            && mAuthorityLeases.renew(current->cell, current->owner, current->leaseId, now)
                == session::LeaseValidation::Valid)
            return mAuthorityLeases.find(cellDescription);
        mAuthorityLeases.release(current->cell, current->owner, current->leaseId);
    }

    const session::LeaseGrantResult result = mAuthorityLeases.grant(
        cellDescription, owner.g, now);
    if (!result.lease || (result.decision != session::LeaseGrantDecision::Granted
            && result.decision != session::LeaseGrantDecision::Existing))
        return std::nullopt;
    return result.lease;
}

bool Networking::validateActorAuthority(const BaseActorList& actorList)
{
    const auto validation = mAuthorityLeases.validateAndRenew(
        actorList.cell.getShortDescription(), actorList.guid.g, actorList.authorityLeaseId,
        session::AuthorityLeaseManager::Clock::now());
    if (validation == session::LeaseValidation::Valid)
        return true;

    const unsigned int violations = ++mAuthorityViolations[actorList.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor simulation update from connection %llu for cell %s: %s (violation %u)",
        static_cast<unsigned long long>(actorList.guid.g),
        actorList.cell.getShortDescription().c_str(), session::describe(validation), violations);
    if (violations >= 5)
        disconnectTransport({ actorList.guid.g }, "repeated invalid actor authority leases");
    return false;
}

bool Networking::releaseActorAuthority(const ESM::Cell& cell, RakNet::RakNetGUID owner,
    std::uint64_t leaseId)
{
    return mAuthorityLeases.release(cell.getShortDescription(), owner.g, leaseId);
}

bool Networking::validatePlayerMovement(Player& player, const BasePlayer& incoming)
{
    mechanics::MovementSample sample;
    sample.position = { incoming.position.pos[0], incoming.position.pos[1],
        incoming.position.pos[2] };
    sample.cell = player.cell.getShortDescription();
    sample.sequence = mCurrentApplicationSequence;
    // Initial synchronization may deliver position before cell state. It is
    // not gameplay movement and cannot establish a meaningful spatial bound.
    if (sample.cell.empty())
        return true;

    const mechanics::MovementValidationResult result = mMovementValidator.validate(
        player.guid.g, sample, mMovementMaximumSpeed,
        mechanics::MovementValidator::Clock::now());
    if (result.accepted())
        return true;

    const unsigned int violations = ++mMovementViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected movement from connection %llu: %s; distance %.3f, allowed %.3f (violation %u)",
        static_cast<unsigned long long>(player.guid.g), mechanics::describe(result.decision),
        result.distance, result.allowedDistance, violations);
    try
    {
        double travelled = result.distance;
        double allowed = result.allowedDistance;
        unsigned int violationCount = violations;
        Script::Call<Script::CallbackIdentity("OnPlayerMovementViolation")>(player.getId(),
            mechanics::describe(result.decision), travelled, allowed, violationCount);
    }
    catch (...)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "%s", "OnPlayerMovementViolation failed; movement remains rejected");
    }
    if (violations >= mMovementViolationLimit)
        disconnectTransport({ player.guid.g }, "repeated invalid movement samples");
    return false;
}

bool Networking::authorizePlayerMovement(const Player& player, double tolerance)
{
    return mMovementValidator.authorizeTransition(player.guid.g,
        player.cell.getShortDescription(),
        { player.position.pos[0], player.position.pos[1], player.position.pos[2] },
        tolerance, mechanics::MovementValidator::Clock::now());
}

void Networking::resetPlayerMovement(std::uint64_t connection) noexcept
{
    mMovementValidator.erase(connection);
    mMovementViolations.erase(connection);
}

bool Networking::acceptPlayerDeath(Player& player)
{
    const mechanics::CombatantId combatant{
        mechanics::CombatantKind::Player, player.guid.g, {} };
    const auto combatState = mCombatResolver.find(combatant);
    if (!combatState || combatState->alive || combatState->health > 0)
    {
        const unsigned int violations = ++mLifecycleViolations[player.guid.g];
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Rejected non-canonical death intent from connection %llu (violation %u)",
            static_cast<unsigned long long>(player.guid.g), violations);
        if (violations >= 5)
            disconnectTransport({ player.guid.g }, "repeated non-canonical death intents");
        return false;
    }

    const mechanics::PlayerLifeState lifeState
        = mPlayerLifecycle.state(player.guid.g);
    if (lifeState == mechanics::PlayerLifeState::Dead
        || lifeState == mechanics::PlayerLifeState::Respawning)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
            "Ignored duplicate death acknowledgement from connection %llu",
            static_cast<unsigned long long>(player.guid.g));
        return false;
    }

    const mechanics::PlayerLifeTransition transition
        = mPlayerLifecycle.reportDeath(player.guid.g);
    if (transition.applied())
        return true;

    const unsigned int violations = ++mLifecycleViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected death intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(transition.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid death intents");
    return false;
}

bool Networking::publishCanonicalPlayerDeath(Player& player, const Target& killer)
{
    const mechanics::PlayerLifeTransition transition
        = mPlayerLifecycle.reportDeath(player.guid.g);
    if (!transition.applied())
    {
        if (transition.decision == mechanics::PlayerLifeDecision::AlreadyDead)
            return false;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Failed to publish canonical death for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.g),
            mechanics::describe(transition.decision));
        return false;
    }

    player.creatureStats.mDead = true;
    player.killer = killer;
    PlayerPacket* deathPacket = playerPacketController->GetPacket(ID_PLAYER_DEATH);
    deathPacket->setPlayer(&player);
    deathPacket->Send(player.guid);
    player.sendToLoaded(deathPacket);
    Script::Call<Script::CallbackIdentity("OnPlayerDeath")>(player.getId());
    return true;
}

bool Networking::beginPlayerRespawn(Player& player, std::uint32_t respawnType)
{
    const mechanics::PlayerLifeTransition transition
        = mPlayerLifecycle.beginRespawn(player.guid.g, respawnType);
    if (transition.applied())
        return true;
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected server respawn transition for connection %llu: %s",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(transition.decision));
    return false;
}

bool Networking::acknowledgePlayerRespawn(Player& player, const BasePlayer& incoming)
{
    const mechanics::PlayerLifeTransition transition
        = mPlayerLifecycle.acknowledgeRespawn(player.guid.g, incoming.resurrectType);
    if (transition.applied())
        return true;

    const unsigned int violations = ++mLifecycleViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected respawn acknowledgement from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(transition.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid respawn acknowledgements");
    return false;
}

namespace
{
    std::optional<mwmp::mechanics::InventoryAction> inventoryAction(int action)
    {
        switch (action)
        {
            case mwmp::InventoryChanges::SET:
                return mwmp::mechanics::InventoryAction::Set;
            case mwmp::InventoryChanges::ADD:
                return mwmp::mechanics::InventoryAction::Add;
            case mwmp::InventoryChanges::REMOVE:
                return mwmp::mechanics::InventoryAction::Remove;
            default:
                return std::nullopt;
        }
    }

    std::vector<mwmp::mechanics::InventoryItem> inventoryItems(
        const mwmp::InventoryChanges& changes)
    {
        std::vector<mwmp::mechanics::InventoryItem> result;
        result.reserve(changes.items.size());
        for (const mwmp::Item& item : changes.items)
        {
            result.push_back({ item.refId, item.soul, item.charge,
                item.enchantmentCharge, item.count });
        }
        return result;
    }

    struct ContainerOperations
    {
        mwmp::mechanics::InventoryDecision decision
            = mwmp::mechanics::InventoryDecision::InvalidAction;
        std::vector<mwmp::mechanics::InventoryOperation> operations;
    };

    ContainerOperations containerOperations(const mwmp::BaseObjectList& objectList,
        const mwmp::mechanics::InventoryLedger& ledger, bool serverAuthored = false)
    {
        ContainerOperations result;
        const auto action = inventoryAction(objectList.action);
        const std::string cellDescription = objectList.cell.getShortDescription();
        if (!action || cellDescription.empty()
            || objectList.baseObjectCount != objectList.baseObjects.size()
            || objectList.containerSubAction > mwmp::BaseObjectList::RESTOCK_RESULT)
        {
            return result;
        }
        if (!serverAuthored && *action == mwmp::mechanics::InventoryAction::Set
            && objectList.containerSubAction != mwmp::BaseObjectList::REPLY_TO_REQUEST
            && objectList.containerSubAction != mwmp::BaseObjectList::RESTOCK_RESULT)
        {
            return result;
        }

        std::unordered_set<mwmp::mechanics::InventoryOwner,
            mwmp::mechanics::InventoryOwnerHash> owners;
        result.operations.reserve(objectList.baseObjects.size());
        for (const mwmp::BaseObject& object : objectList.baseObjects)
        {
            if ((object.refNum == 0) == (object.mpNum == 0)
                || object.containerItemCount != object.containerItems.size())
            {
                result.decision = mwmp::mechanics::InventoryDecision::InvalidOwner;
                return result;
            }

            const std::uint64_t reference = (static_cast<std::uint64_t>(object.refNum) << 32)
                | static_cast<std::uint64_t>(object.mpNum);
            mwmp::mechanics::InventoryOwner owner{
                mwmp::mechanics::InventoryOwnerKind::Container,
                reference, cellDescription };
            if (!owners.insert(owner).second)
            {
                result.decision = mwmp::mechanics::InventoryDecision::InvalidOwner;
                return result;
            }

            const bool exists = ledger.snapshot(owner).has_value();
            const bool changesUnknownState
                = *action != mwmp::mechanics::InventoryAction::Set && !exists;
            if (!serverAuthored && changesUnknownState)
            {
                result.decision = mwmp::mechanics::InventoryDecision::InvalidAction;
                return result;
            }

            mwmp::mechanics::InventoryOperation operation;
            operation.owner = std::move(owner);
            operation.action = *action;
            operation.items.reserve(object.containerItems.size());
            for (const mwmp::ContainerItem& item : object.containerItems)
            {
                const std::int64_t count
                    = *action == mwmp::mechanics::InventoryAction::Remove
                    ? static_cast<std::int64_t>(item.actionCount)
                    : static_cast<std::int64_t>(item.count);
                operation.items.push_back({ item.refId, item.soul, item.charge,
                    item.enchantmentCharge, count });
            }
            result.operations.push_back(std::move(operation));
        }
        result.decision = mwmp::mechanics::InventoryDecision::Applied;
        return result;
    }
}

bool Networking::validatePlayerInventory(Player& player, const BasePlayer& incoming)
{
    const auto action = inventoryAction(incoming.inventoryChanges.action);
    mechanics::InventoryResult result{ mechanics::InventoryDecision::InvalidAction };
    if (action)
    {
        result = mInventoryLedger.preview(
            { mechanics::InventoryOwnerKind::Player, player.guid.g }, *action,
            inventoryItems(incoming.inventoryChanges));
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected inventory action from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid inventory actions");
    return false;
}

bool Networking::commitPlayerInventory(Player& player)
{
    const auto action = inventoryAction(player.inventoryChanges.action);
    mechanics::InventoryResult result{ mechanics::InventoryDecision::InvalidAction };
    if (action)
    {
        result = mInventoryLedger.apply(
            { mechanics::InventoryOwnerKind::Player, player.guid.g }, *action,
            inventoryItems(player.inventoryChanges));
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified inventory intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid inventory actions");
    return false;
}

bool Networking::applyServerInventoryChanges(Player& player)
{
    const auto action = inventoryAction(player.inventoryChanges.action);
    if (!action)
        return false;
    const mechanics::InventoryResult result = mInventoryLedger.apply(
        { mechanics::InventoryOwnerKind::Player, player.guid.g }, *action,
        inventoryItems(player.inventoryChanges));
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored inventory action for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.g),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::validateContainerAction(Player& player, const BaseObjectList& incoming)
{
    const ContainerOperations operations = containerOperations(incoming, mInventoryLedger);
    mechanics::InventoryResult result{ operations.decision };
    if (operations.decision == mechanics::InventoryDecision::Applied)
        result = mInventoryLedger.previewBatch(operations.operations);
    if (result.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected container action from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid container actions");
    return false;
}

bool Networking::commitContainerAction(Player& player, const BaseObjectList& incoming)
{
    const ContainerOperations operations = containerOperations(incoming, mInventoryLedger);
    mechanics::InventoryResult result{ operations.decision };
    if (operations.decision == mechanics::InventoryDecision::Applied)
        result = mInventoryLedger.applyBatch(operations.operations);
    if (result.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected container intent at commit for connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid container actions");
    return false;
}

bool Networking::seedServerContainerInventory(const BaseObjectList& objectList)
{
    BaseObjectList normalized = objectList;
    normalized.action = BaseObjectList::SET;
    normalized.containerSubAction = BaseObjectList::NONE;
    normalized.baseObjectCount = normalized.baseObjects.size();
    for (BaseObject& object : normalized.baseObjects)
        object.containerItemCount = object.containerItems.size();

    const ContainerOperations operations
        = containerOperations(normalized, mInventoryLedger, true);
    mechanics::InventoryResult result{ operations.decision };
    if (operations.decision == mechanics::InventoryDecision::Applied)
        result = mInventoryLedger.applyBatch(operations.operations);
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Failed to seed canonical container inventory for %s: %s",
            normalized.cell.getShortDescription().c_str(),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

namespace
{
    std::optional<mwmp::mechanics::ActiveEffectAction> activeEffectAction(int action)
    {
        switch (action)
        {
            case mwmp::SpellsActiveChanges::SET:
                return mwmp::mechanics::ActiveEffectAction::Set;
            case mwmp::SpellsActiveChanges::ADD:
                return mwmp::mechanics::ActiveEffectAction::Add;
            case mwmp::SpellsActiveChanges::REMOVE:
                return mwmp::mechanics::ActiveEffectAction::Remove;
            default:
                return std::nullopt;
        }
    }

    std::uint64_t activeEffectReference(unsigned int refNum, unsigned int mpNum) noexcept
    {
        return (static_cast<std::uint64_t>(refNum) << 32)
            | static_cast<std::uint64_t>(mpNum);
    }

    bool canonicalActiveSpells(const mwmp::SpellsActiveChanges& changes,
        std::string_view actorScope,
        std::vector<mwmp::mechanics::CanonicalActiveSpell>& result)
    {
        result.clear();
        result.reserve(changes.activeSpells.size());
        for (const mwmp::ActiveSpell& spell : changes.activeSpells)
        {
            mwmp::mechanics::CanonicalActiveSpell canonical;
            canonical.id = spell.id;
            canonical.displayName = spell.params.mDisplayName;
            canonical.stacking = spell.isStackingSpell;
            canonical.timestampDay = spell.timestampDay;
            canonical.timestampHour = spell.timestampHour;

            if (spell.caster.isPlayer)
            {
                if (spell.caster.guid.g == 0)
                    return false;
                canonical.caster = mwmp::mechanics::CombatantId{
                    mwmp::mechanics::CombatantKind::Player, spell.caster.guid.g, {} };
            }
            else
            {
                const bool hasRefNum = spell.caster.refNum != 0;
                const bool hasMpNum = spell.caster.mpNum != 0;
                if (hasRefNum && hasMpNum)
                    return false;
                if (hasRefNum || hasMpNum)
                {
                    if (actorScope.empty())
                        return false;
                    canonical.caster = mwmp::mechanics::CombatantId{
                        mwmp::mechanics::CombatantKind::Actor,
                        activeEffectReference(spell.caster.refNum, spell.caster.mpNum),
                        std::string(actorScope) };
                }
                else if (!spell.caster.refId.empty())
                    return false;
            }

            canonical.effects.reserve(spell.params.mEffects.size());
            for (const ESM::ActiveEffect& effect : spell.params.mEffects)
            {
                mwmp::mechanics::CanonicalEffect canonicalEffect;
                canonicalEffect.effectId = effect.mEffectId.getRefIdString();
                if (const ESM::RefId* argument = std::get_if<ESM::RefId>(&effect.mArg))
                    canonicalEffect.argument = argument->getRefIdString();
                else
                {
                    const ESM::FormId& formArgument
                        = std::get<ESM::FormId>(effect.mArg);
                    canonicalEffect.argument = std::to_string(formArgument.mContentFile)
                        + ":" + std::to_string(formArgument.mIndex);
                }
                canonicalEffect.magnitude = effect.mMagnitude;
                canonicalEffect.duration = effect.mDuration;
                canonicalEffect.timeLeft = effect.mTimeLeft;
                canonical.effects.push_back(std::move(canonicalEffect));
            }
            result.push_back(std::move(canonical));
        }
        return true;
    }

    struct ActiveEffectOperations
    {
        mwmp::mechanics::ActiveEffectDecision decision
            = mwmp::mechanics::ActiveEffectDecision::InvalidAction;
        std::vector<mwmp::mechanics::ActiveEffectOperation> operations;
    };

    ActiveEffectOperations actorActiveEffectOperations(
        const mwmp::BaseActorList& actorList)
    {
        ActiveEffectOperations result;
        const std::string cellDescription = actorList.cell.getShortDescription();
        if (cellDescription.empty()
            || actorList.count != actorList.baseActors.size())
            return result;

        result.operations.reserve(actorList.baseActors.size());
        for (const mwmp::BaseActor& actor : actorList.baseActors)
        {
            const bool hasRefNum = actor.refNum != 0;
            const bool hasMpNum = actor.mpNum != 0;
            const auto action = activeEffectAction(actor.spellsActiveChanges.action);
            if (hasRefNum == hasMpNum || !action)
            {
                result.decision = mwmp::mechanics::ActiveEffectDecision::InvalidOwner;
                return result;
            }

            mwmp::mechanics::ActiveEffectOperation operation;
            operation.owner = { mwmp::mechanics::CombatantKind::Actor,
                activeEffectReference(actor.refNum, actor.mpNum), cellDescription };
            operation.action = *action;
            if (!canonicalActiveSpells(actor.spellsActiveChanges, cellDescription,
                    operation.spells))
            {
                result.decision = mwmp::mechanics::ActiveEffectDecision::InvalidSpell;
                return result;
            }
            result.operations.push_back(std::move(operation));
        }
        result.decision = mwmp::mechanics::ActiveEffectDecision::Applied;
        return result;
    }
}

bool Networking::validatePlayerActiveEffects(Player& player, const BasePlayer& incoming)
{
    const auto action = activeEffectAction(incoming.spellsActiveChanges.action);
    std::vector<mechanics::CanonicalActiveSpell> spells;
    mechanics::ActiveEffectResult result{
        mechanics::ActiveEffectDecision::InvalidAction };
    if (action && canonicalActiveSpells(incoming.spellsActiveChanges,
            player.cell.getShortDescription(), spells))
    {
        result = mActiveEffectLedger.preview(
            { mechanics::CombatantKind::Player, player.guid.g, {} }, *action, spells);
    }
    else if (action)
        result.decision = mechanics::ActiveEffectDecision::InvalidSpell;

    if (result.applied())
        return true;
    const unsigned int violations = ++mActiveEffectViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected active-effect change from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid active-effect changes");
    return false;
}

bool Networking::commitPlayerActiveEffects(Player& player)
{
    const auto action = activeEffectAction(player.spellsActiveChanges.action);
    std::vector<mechanics::CanonicalActiveSpell> spells;
    mechanics::ActiveEffectResult result{
        mechanics::ActiveEffectDecision::InvalidAction };
    mechanics::ActiveEffectOperation operation;
    operation.owner = { mechanics::CombatantKind::Player, player.guid.g, {} };
    if (action && canonicalActiveSpells(player.spellsActiveChanges,
            player.cell.getShortDescription(), spells))
    {
        operation.action = *action;
        operation.spells = spells;
        result = mActiveEffectLedger.apply(
            operation.owner, operation.action, operation.spells);
    }
    else if (action)
        result.decision = mechanics::ActiveEffectDecision::InvalidSpell;

    if (result.applied())
    {
        mAcceptedPlayerActiveEffectIntents.insert_or_assign(
            player.guid.g, std::move(operation));
        mRelayedPlayerActiveEffectIntents.erase(player.guid.g);
        return true;
    }
    const unsigned int violations = ++mActiveEffectViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified active-effect intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid active-effect changes");
    return false;
}

bool Networking::applyServerPlayerActiveEffects(Player& player)
{
    const auto action = activeEffectAction(player.spellsActiveChanges.action);
    std::vector<mechanics::CanonicalActiveSpell> spells;
    mechanics::ActiveEffectResult result{
        mechanics::ActiveEffectDecision::InvalidAction };
    mechanics::ActiveEffectOperation operation;
    operation.owner = { mechanics::CombatantKind::Player, player.guid.g, {} };
    if (action && canonicalActiveSpells(player.spellsActiveChanges,
            player.cell.getShortDescription(), spells))
    {
        operation.action = *action;
        operation.spells = std::move(spells);
        const auto accepted = mAcceptedPlayerActiveEffectIntents.find(player.guid.g);
        if (accepted != mAcceptedPlayerActiveEffectIntents.end()
            && accepted->second == operation)
        {
            mRelayedPlayerActiveEffectIntents.insert(player.guid.g);
            return true;
        }
        result = mActiveEffectLedger.apply(
            operation.owner, operation.action, operation.spells);
    }
    else if (action)
        result.decision = mechanics::ActiveEffectDecision::InvalidSpell;
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored active-effect change for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.g),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::finishPlayerActiveEffectIntent(Player& player) noexcept
{
    mAcceptedPlayerActiveEffectIntents.erase(player.guid.g);
    return mRelayedPlayerActiveEffectIntents.erase(player.guid.g) != 0;
}

bool Networking::validateActorActiveEffects(Player& player,
    const BaseActorList& incoming)
{
    const ActiveEffectOperations operations = actorActiveEffectOperations(incoming);
    mechanics::ActiveEffectResult result{ operations.decision };
    if (operations.decision == mechanics::ActiveEffectDecision::Applied)
        result = mActiveEffectLedger.previewBatch(operations.operations);
    if (result.applied())
        return true;

    const unsigned int violations = ++mActiveEffectViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor active-effect change from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid actor active-effect changes");
    return false;
}

bool Networking::commitActorActiveEffects(Player& player,
    const BaseActorList& incoming)
{
    ActiveEffectOperations operations = actorActiveEffectOperations(incoming);
    mechanics::ActiveEffectResult result{ operations.decision };
    if (operations.decision == mechanics::ActiveEffectDecision::Applied)
        result = mActiveEffectLedger.applyBatch(operations.operations);
    if (result.applied())
    {
        mAcceptedActorActiveEffectIntents.insert_or_assign(player.guid.g,
            std::move(operations.operations));
        mRelayedActorActiveEffectIntents.erase(player.guid.g);
        return true;
    }

    const unsigned int violations = ++mActiveEffectViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified actor active-effect intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid actor active-effect changes");
    return false;
}

bool Networking::applyServerActorActiveEffects(const BaseActorList& actorList)
{
    const ActiveEffectOperations operations = actorActiveEffectOperations(actorList);
    mechanics::ActiveEffectResult result{ operations.decision };
    if (operations.decision == mechanics::ActiveEffectDecision::Applied)
    {
        const auto accepted = mAcceptedActorActiveEffectIntents.find(actorList.guid.g);
        if (accepted != mAcceptedActorActiveEffectIntents.end()
            && accepted->second == operations.operations)
        {
            mRelayedActorActiveEffectIntents.insert(actorList.guid.g);
            return true;
        }
        result = mActiveEffectLedger.applyBatch(operations.operations);
    }
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored actor active-effect change for %s: %s",
            actorList.cell.getShortDescription().c_str(),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::finishActorActiveEffectIntent(Player& player) noexcept
{
    mAcceptedActorActiveEffectIntents.erase(player.guid.g);
    return mRelayedActorActiveEffectIntents.erase(player.guid.g) != 0;
}

namespace
{
    mwmp::mechanics::TransformIntent castTransform(const ESM::Position& value)
    {
        return {
            { value.pos[0], value.pos[1], value.pos[2] },
            { value.rot[0], value.rot[1], value.rot[2] }
        };
    }

    mwmp::mechanics::CastIntentDecision canonicalCastTarget(
        const mwmp::Target& target, std::string_view actorScope,
        std::optional<mwmp::mechanics::CombatantId>& result)
    {
        result.reset();
        if (target.isPlayer)
        {
            if (target.guid.g == 0)
                return mwmp::mechanics::CastIntentDecision::InvalidTarget;
            result = mwmp::mechanics::CombatantId{
                mwmp::mechanics::CombatantKind::Player, target.guid.g, {} };
            return mwmp::mechanics::CastIntentDecision::Accepted;
        }

        const bool hasRefNum = target.refNum != 0;
        const bool hasMpNum = target.mpNum != 0;
        if (!hasRefNum && !hasMpNum && target.refId.empty())
            return mwmp::mechanics::CastIntentDecision::Accepted;
        if (hasRefNum == hasMpNum || actorScope.empty() || target.refId.empty()
            || target.refId.size()
                > mwmp::mechanics::CastIntentValidator::MaximumSourceIdBytes)
        {
            return mwmp::mechanics::CastIntentDecision::InvalidTarget;
        }
        result = mwmp::mechanics::CombatantId{
            mwmp::mechanics::CombatantKind::Actor,
            activeEffectReference(target.refNum, target.mpNum),
            std::string(actorScope) };
        return mwmp::mechanics::CastIntentDecision::Accepted;
    }

    mwmp::mechanics::CastIntentDecision canonicalCastIntent(
        mwmp::mechanics::CombatantId caster, const mwmp::Cast& cast,
        std::string_view actorScope, mwmp::mechanics::CastIntent& result,
        const ESM::Position* reportedPosition = nullptr,
        const ESM::Position* canonicalPosition = nullptr,
        const ESM::Position* reportedDirection = nullptr)
    {
        result = {};
        result.caster = std::move(caster);
        const auto targetDecision = canonicalCastTarget(
            cast.target, actorScope, result.target);
        if (targetDecision != mwmp::mechanics::CastIntentDecision::Accepted)
            return targetDecision;

        if (cast.type == mwmp::Cast::REGULAR)
        {
            result.kind = mwmp::mechanics::CastKind::Regular;
            result.sourceId = cast.spellId;
            result.pressed = cast.pressed;
            result.instant = cast.instant;
        }
        else if (cast.type == mwmp::Cast::ITEM)
        {
            result.kind = mwmp::mechanics::CastKind::Item;
            result.sourceId = cast.itemId;
        }
        else
            return mwmp::mechanics::CastIntentDecision::InvalidSource;

        if (cast.hasProjectile)
        {
            result.projectile = mwmp::mechanics::ProjectileIntent{
                { cast.projectileOrigin.origin[0], cast.projectileOrigin.origin[1],
                    cast.projectileOrigin.origin[2] },
                { cast.projectileOrigin.orientation[0],
                    cast.projectileOrigin.orientation[1],
                    cast.projectileOrigin.orientation[2],
                    cast.projectileOrigin.orientation[3] }
            };
            if (reportedPosition != nullptr && canonicalPosition != nullptr
                && reportedDirection != nullptr)
            {
                result.reportedCasterTransform = castTransform(*reportedPosition);
                result.canonicalCasterTransform = castTransform(*canonicalPosition);
                result.reportedCasterDirection = castTransform(*reportedDirection);
            }
        }
        return mwmp::mechanics::CastIntentDecision::Accepted;
    }
}

bool Networking::validatePlayerCast(Player& player, const BasePlayer& incoming)
{
    mechanics::CastIntent intent;
    mechanics::CastIntentDecision decision = canonicalCastIntent(
        { mechanics::CombatantKind::Player, player.guid.g, {} }, incoming.cast,
        player.cell.getShortDescription(), intent,
        incoming.cast.hasProjectile ? &incoming.position : nullptr,
        incoming.cast.hasProjectile ? &player.position : nullptr,
        incoming.cast.hasProjectile ? &incoming.direction : nullptr);
    if (decision == mechanics::CastIntentDecision::Accepted)
        decision = mCastIntentValidator.validate(intent);

    if (decision == mechanics::CastIntentDecision::Accepted && intent.target)
    {
        if (intent.target->kind == mechanics::CombatantKind::Player)
        {
            const Player* target = Players::getPlayer(
                RakNet::RakNetGUID(intent.target->value));
            if (target == nullptr
                || !mAuthenticatedConnections.contains(target->guid.g)
                || target->cell.getShortDescription()
                    != player.cell.getShortDescription())
            {
                decision = mechanics::CastIntentDecision::InvalidTarget;
            }
        }
        else
        {
            Cell* cell = CellController::get()->getCell(&player.cell);
            if (cell == nullptr || cell->getActor(incoming.cast.target.refNum,
                    incoming.cast.target.mpNum) == nullptr)
            {
                decision = mechanics::CastIntentDecision::InvalidTarget;
            }
        }
    }
    if (decision == mechanics::CastIntentDecision::Accepted)
        return true;

    const unsigned int violations = ++mCastViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid cast intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid cast intents");
    return false;
}

bool Networking::validateActorCasts(Player& player, const BaseActorList& incoming)
{
    mechanics::CastIntentDecision decision = mechanics::CastIntentDecision::Accepted;
    const std::string cellDescription = incoming.cell.getShortDescription();
    Cell* cell = incoming.cell.isExterior()
        ? CellController::get()->getCellByXY(
            incoming.cell.mData.mX, incoming.cell.mData.mY)
        : CellController::get()->getCellByName(incoming.cell.mName);
    if (cellDescription.empty() || incoming.count != incoming.baseActors.size()
        || cell == nullptr)
    {
        decision = mechanics::CastIntentDecision::InvalidCaster;
    }

    for (const BaseActor& actor : incoming.baseActors)
    {
        if (decision != mechanics::CastIntentDecision::Accepted)
            break;
        const bool hasRefNum = actor.refNum != 0;
        const bool hasMpNum = actor.mpNum != 0;
        if (hasRefNum == hasMpNum
            || cell->getActor(actor.refNum, actor.mpNum) == nullptr)
        {
            decision = mechanics::CastIntentDecision::InvalidCaster;
            break;
        }

        mechanics::CastIntent intent;
        decision = canonicalCastIntent(
            { mechanics::CombatantKind::Actor,
                activeEffectReference(actor.refNum, actor.mpNum), cellDescription },
            actor.cast, cellDescription, intent);
        if (decision == mechanics::CastIntentDecision::Accepted)
            decision = mCastIntentValidator.validate(intent);
        if (decision == mechanics::CastIntentDecision::Accepted && intent.target)
        {
            if (intent.target->kind == mechanics::CombatantKind::Player)
            {
                const Player* target = Players::getPlayer(
                    RakNet::RakNetGUID(intent.target->value));
                if (target == nullptr
                    || !mAuthenticatedConnections.contains(target->guid.g)
                    || target->cell.getShortDescription() != cellDescription)
                {
                    decision = mechanics::CastIntentDecision::InvalidTarget;
                }
            }
            else if (cell->getActor(actor.cast.target.refNum,
                         actor.cast.target.mpNum) == nullptr)
            {
                decision = mechanics::CastIntentDecision::InvalidTarget;
            }
        }
    }
    if (decision == mechanics::CastIntentDecision::Accepted)
        return true;

    const unsigned int violations = ++mCastViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid actor cast list from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.g),
        mechanics::describe(decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid actor cast intents");
    return false;
}

namespace
{
    constexpr double maximumCanonicalStat =
        mwmp::mechanics::CombatResolver::MaximumStatValue;

    bool validDynamicStat(const ESM::StatState<float>& stat, bool health) noexcept
    {
        const auto finiteBounded = [](float value) {
            return std::isfinite(value)
                && std::abs(static_cast<double>(value)) <= maximumCanonicalStat;
        };
        if (!finiteBounded(stat.mBase) || !finiteBounded(stat.mMod)
            || !finiteBounded(stat.mCurrent) || !finiteBounded(stat.mDamage)
            || !finiteBounded(stat.mProgress))
            return false;
        return !health || (stat.mBase >= 0 && stat.mMod >= 0 && stat.mCurrent >= 0);
    }

    double dynamicMaximum(const ESM::StatState<float>& stat) noexcept
    {
        return std::max({ 1.0, static_cast<double>(stat.mBase),
            static_cast<double>(stat.mMod), static_cast<double>(stat.mCurrent) });
    }

    double fatigueRatio(const ESM::StatState<float>& stat) noexcept
    {
        const double maximum = dynamicMaximum(stat);
        return std::clamp(static_cast<double>(stat.mCurrent) / maximum, 0.0, 1.0);
    }

    mwmp::mechanics::CombatantState playerCombatState(const Player& player,
        const std::optional<mwmp::mechanics::CombatantState>& existing,
        bool replaceHealth)
    {
        mwmp::mechanics::CombatantState state = existing.value_or(
            mwmp::mechanics::CombatantState{});
        const auto& health = player.creatureStats.mDynamic[0];
        if (!existing || replaceHealth)
        {
            state.health = std::clamp(static_cast<double>(health.mCurrent),
                0.0, maximumCanonicalStat);
            state.maximumHealth = dynamicMaximum(health);
            state.alive = state.health > 0;
        }
        else
            state.maximumHealth = std::max(state.maximumHealth, state.health);
        state.fatigueRatio = fatigueRatio(player.creatureStats.mDynamic[2]);
        state.accuracy = 0.75;
        state.evasion = 0.10;
        state.armorRating = 0;
        state.minimumDamage = 1;
        state.maximumDamage = 12;
        state.meleeReach = 192;
        state.projectileReach = 8192;
        state.position = { player.position.pos[0], player.position.pos[1],
            player.position.pos[2] };
        return state;
    }

    void applyCanonicalHealth(Player& player,
        const mwmp::mechanics::CombatantState& state) noexcept
    {
        auto& health = player.creatureStats.mDynamic[0];
        health.mBase = static_cast<float>(state.maximumHealth);
        health.mMod = static_cast<float>(state.maximumHealth);
        health.mCurrent = static_cast<float>(state.health);
        health.mDamage = 0;
        health.mProgress = 0;
        player.creatureStats.mDead = !state.alive;
    }

    std::uint64_t actorReferenceValue(const mwmp::BaseActor& actor) noexcept
    {
        return (static_cast<std::uint64_t>(actor.refNum) << 32)
            | static_cast<std::uint64_t>(actor.mpNum);
    }

    mwmp::mechanics::CombatantId actorCombatantId(
        const ESM::Cell& cell, const mwmp::BaseActor& actor)
    {
        return { mwmp::mechanics::CombatantKind::Actor,
            actorReferenceValue(actor), cell.getShortDescription() };
    }

    mwmp::mechanics::CombatantState actorCombatState(const mwmp::BaseActor& actor,
        const mwmp::BaseActor* cachedActor,
        const std::optional<mwmp::mechanics::CombatantState>& existing,
        bool replaceHealth)
    {
        mwmp::mechanics::CombatantState state = existing.value_or(
            mwmp::mechanics::CombatantState{});
        const auto& health = actor.creatureStats.mDynamic[0];
        if (!existing || replaceHealth)
        {
            state.health = std::clamp(static_cast<double>(health.mCurrent),
                0.0, maximumCanonicalStat);
            state.maximumHealth = dynamicMaximum(health);
            state.alive = state.health > 0;
        }
        else
            state.maximumHealth = std::max(state.maximumHealth, state.health);
        state.fatigueRatio = fatigueRatio(actor.creatureStats.mDynamic[2]);
        state.accuracy = 0.70;
        state.evasion = 0.10;
        state.armorRating = 0;
        state.minimumDamage = 1;
        state.maximumDamage = 12;
        state.meleeReach = 192;
        state.projectileReach = 8192;
        const ESM::Position& position = cachedActor != nullptr
            ? cachedActor->position : actor.position;
        state.position = { position.pos[0], position.pos[1], position.pos[2] };
        return state;
    }

    void applyCanonicalHealth(mwmp::BaseActor& actor,
        const mwmp::mechanics::CombatantState& state) noexcept
    {
        auto& health = actor.creatureStats.mDynamic[0];
        health.mBase = static_cast<float>(state.maximumHealth);
        health.mMod = static_cast<float>(state.maximumHealth);
        health.mCurrent = static_cast<float>(state.health);
        health.mDamage = 0;
        health.mProgress = 0;
        actor.creatureStats.mDead = !state.alive;
    }
}

bool Networking::validatePlayerStats(Player& player, const BasePlayer& incoming)
{
    bool valid = true;
    const mechanics::CombatantId id{ mechanics::CombatantKind::Player, player.guid.g, {} };
    const bool hasCanonicalState = mCombatResolver.find(id).has_value();
    bool includesHealth = incoming.exchangeFullInfo;
    if (incoming.exchangeFullInfo)
    {
        for (std::size_t index = 0; index < incoming.creatureStats.mDynamic.size(); ++index)
            valid = valid && validDynamicStat(incoming.creatureStats.mDynamic[index], index == 0);
    }
    else
    {
        for (const std::uint8_t index : incoming.statsDynamicIndexChanges)
        {
            includesHealth = includesHealth || index == 0;
            if (index >= incoming.creatureStats.mDynamic.size()
                || !validDynamicStat(incoming.creatureStats.mDynamic[index], index == 0))
            {
                valid = false;
                break;
            }
        }
    }
    valid = valid && (hasCanonicalState || includesHealth);
    if (valid)
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid dynamic stats from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.g), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid dynamic stats");
    return false;
}

bool Networking::reconcilePlayerStats(Player& player)
{
    const mechanics::CombatantId id{ mechanics::CombatantKind::Player, player.guid.g, {} };
    const auto existing = mCombatResolver.find(id);
    mechanics::CombatantState state = playerCombatState(player, existing, false);
    if (existing)
        applyCanonicalHealth(player, state);
    if (mCombatResolver.upsert(id, state))
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Failed to reconcile canonical stats for connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.g), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid canonical stats");
    return false;
}

bool Networking::applyServerPlayerStats(Player& player)
{
    const mechanics::CombatantId id{ mechanics::CombatantKind::Player, player.guid.g, {} };
    mechanics::CombatantState state = playerCombatState(
        player, mCombatResolver.find(id), true);
    if (!mCombatResolver.upsert(id, state))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored dynamic stats for connection %llu",
            static_cast<unsigned long long>(player.guid.g));
        return false;
    }
    applyCanonicalHealth(player, state);
    return true;
}

bool Networking::validateActorStats(Player& player, const BaseActorList& incoming)
{
    bool valid = !incoming.cell.getShortDescription().empty();
    for (const BaseActor& actor : incoming.baseActors)
    {
        valid = valid && actorReferenceValue(actor) != 0;
        for (std::size_t index = 0; valid && index < 3; ++index)
            valid = validDynamicStat(actor.creatureStats.mDynamic[index], index == 0);
        if (!valid)
            break;
    }
    if (valid)
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid actor stats from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.g), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid actor stats");
    return false;
}

bool Networking::reconcileActorStats(Player& player, BaseActorList& incoming)
{
    Cell* serverCell = CellController::get()->getCell(&incoming.cell);
    if (serverCell == nullptr)
        return false;

    for (BaseActor& actor : incoming.baseActors)
    {
        const mechanics::CombatantId id = actorCombatantId(incoming.cell, actor);
        const auto existing = mCombatResolver.find(id);
        const BaseActor* cachedActor = serverCell->getActor(actor.refNum, actor.mpNum);
        mechanics::CombatantState state = actorCombatState(
            actor, cachedActor, existing, false);
        if (existing)
            applyCanonicalHealth(actor, state);
        if (!mCombatResolver.upsert(id, state))
        {
            const unsigned int violations = ++mCombatViolations[player.guid.g];
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to reconcile canonical actor stats from connection %llu (violation %u)",
                static_cast<unsigned long long>(player.guid.g), violations);
            if (violations >= 5)
                disconnectTransport({ player.guid.g }, "repeated invalid canonical actor stats");
            return false;
        }
    }
    return true;
}

bool Networking::applyServerActorStats(BaseActorList& actorList)
{
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    if (serverCell == nullptr || actorList.cell.getShortDescription().empty())
        return false;

    for (BaseActor& actor : actorList.baseActors)
    {
        if (actorReferenceValue(actor) == 0)
            return false;
        for (std::size_t index = 0; index < 3; ++index)
        {
            if (!validDynamicStat(actor.creatureStats.mDynamic[index], index == 0))
                return false;
        }

        const mechanics::CombatantId id = actorCombatantId(actorList.cell, actor);
        const BaseActor* cachedActor = serverCell->getActor(actor.refNum, actor.mpNum);
        mechanics::CombatantState state = actorCombatState(
            actor, cachedActor, mCombatResolver.find(id), true);
        if (!mCombatResolver.upsert(id, state))
            return false;
        applyCanonicalHealth(actor, state);
    }
    serverCell->readActorList(ID_ACTOR_STATS_DYNAMIC, &actorList);
    return true;
}

bool Networking::validatePlayerAttack(Player& player, const BasePlayer& incoming)
{
    const Attack& attack = incoming.attack;
    bool valid = attack.type == Attack::MELEE || attack.type == Attack::RANGED;
    valid = valid && attack.attackAnimation.size() <= 128
        && attack.rangedWeaponId.size() <= 256 && attack.rangedAmmoId.size() <= 256;

    if (attack.type == Attack::RANGED)
    {
        valid = valid && std::isfinite(attack.attackStrength)
            && attack.attackStrength >= 0 && attack.attackStrength <= 1;
        for (const float coordinate : attack.projectileOrigin.origin)
            valid = valid && std::isfinite(coordinate);
        for (const float coordinate : attack.projectileOrigin.orientation)
            valid = valid && std::isfinite(coordinate);
    }

    if (!attack.pressed)
    {
        if (attack.target.isPlayer)
            valid = valid && attack.target.guid.g != 0 && attack.target.guid != player.guid;
        else
            valid = valid && (attack.target.refNum != 0 || attack.target.mpNum != 0)
                && !(attack.target.refNum != 0 && attack.target.mpNum != 0);
    }
    if (valid)
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid attack intent from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.g), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid attack intents");
    return false;
}

void Networking::sanitizePlayerAttack(Player& player) noexcept
{
    player.attack.success = false;
    player.attack.isHit = false;
    player.attack.damage = 0;
    player.attack.block = false;
    player.attack.knockdown = false;
    player.attack.applyWeaponEnchantment = false;
    player.attack.applyAmmoEnchantment = false;
}

bool Networking::resolvePlayerAttack(Player& player, std::string& rejectionReason)
{
    rejectionReason.clear();
    const mechanics::CombatantId attackerId{
        mechanics::CombatantKind::Player, player.guid.g, {} };
    auto attackerState = mCombatResolver.find(attackerId);
    if (!attackerState)
    {
        rejectionReason = "the attacker has no canonical combat state";
        return false;
    }
    attackerState->position = { player.position.pos[0], player.position.pos[1],
        player.position.pos[2] };
    if (!mCombatResolver.upsert(attackerId, *attackerState))
    {
        rejectionReason = "the attacker canonical state is invalid";
        return false;
    }

    mechanics::CombatantId targetId;
    Player* targetPlayer = nullptr;
    Cell* targetCell = nullptr;
    BaseActor* targetActor = nullptr;
    if (player.attack.target.isPlayer)
    {
        targetPlayer = Players::getPlayer(player.attack.target.guid);
        if (targetPlayer == nullptr
            || !mAuthenticatedConnections.contains(targetPlayer->guid.g)
            || targetPlayer->cell.getShortDescription() != player.cell.getShortDescription())
        {
            rejectionReason = "the target player is unavailable or in another cell";
            return false;
        }
        targetId = { mechanics::CombatantKind::Player, targetPlayer->guid.g, {} };
        auto state = mCombatResolver.find(targetId);
        if (!state)
        {
            rejectionReason = "the target player has no canonical combat state";
            return false;
        }
        state->position = { targetPlayer->position.pos[0], targetPlayer->position.pos[1],
            targetPlayer->position.pos[2] };
        if (!mCombatResolver.upsert(targetId, *state))
        {
            rejectionReason = "the target player canonical state is invalid";
            return false;
        }
    }
    else
    {
        targetCell = CellController::get()->getCell(&player.cell);
        if (targetCell != nullptr)
            targetActor = targetCell->getActor(
                player.attack.target.refNum, player.attack.target.mpNum);
        if (targetActor == nullptr)
        {
            rejectionReason = "the target actor is absent from canonical cell state";
            return false;
        }
        targetId = actorCombatantId(player.cell, *targetActor);
        auto state = mCombatResolver.find(targetId);
        if (!state && targetActor->hasStatsDynamicData)
        {
            const mechanics::CombatantState initial = actorCombatState(
                *targetActor, targetActor, std::nullopt, true);
            if (mCombatResolver.upsert(targetId, initial))
                state = initial;
        }
        if (!state)
        {
            rejectionReason = "the target actor has no canonical combat state";
            return false;
        }
        state->position = { targetActor->position.pos[0], targetActor->position.pos[1],
            targetActor->position.pos[2] };
        if (!mCombatResolver.upsert(targetId, *state))
        {
            rejectionReason = "the target actor canonical state is invalid";
            return false;
        }
    }

    const double strength = player.attack.type == Attack::RANGED
        ? static_cast<double>(player.attack.attackStrength) : 1.0;
    if (!std::isfinite(strength) || strength < 0 || strength > 1)
    {
        rejectionReason = "the script-modified attack strength is invalid";
        return false;
    }

    const mechanics::AttackIntent intent{
        attackerId, targetId, mCurrentApplicationSequence,
        player.attack.type == Attack::RANGED
            ? mechanics::AttackKind::Ranged : mechanics::AttackKind::Melee,
        strength };
    constexpr double randomScale = 1.0 / 4294967296.0;
    const mechanics::CombatResult result = mCombatResolver.resolve(
        intent, static_cast<double>(randombytes_random()) * randomScale);
    if (!result.applied())
    {
        rejectionReason = mechanics::describe(result.decision);
        return false;
    }

    player.attack.success = result.decision == mechanics::CombatDecision::AppliedHit;
    player.attack.isHit = player.attack.success;
    player.attack.damage = static_cast<float>(result.damage);

    const auto canonicalTarget = mCombatResolver.find(targetId);
    if (!canonicalTarget)
    {
        rejectionReason = "the canonical target disappeared after combat resolution";
        return false;
    }

    if (targetPlayer != nullptr)
    {
        applyCanonicalHealth(*targetPlayer, *canonicalTarget);
        targetPlayer->exchangeFullInfo = false;
        targetPlayer->statsDynamicIndexChanges.clear();
        targetPlayer->statsDynamicIndexChanges.push_back(0);
        PlayerPacket* statsPacket = playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC);
        statsPacket->setPlayer(targetPlayer);
        statsPacket->Send(targetPlayer->guid);
        targetPlayer->sendToLoaded(statsPacket);
        if (result.targetDied)
        {
            Target killer;
            killer.isPlayer = true;
            killer.guid = player.guid;
            publishCanonicalPlayerDeath(*targetPlayer, killer);
        }
    }
    else
    {
        applyCanonicalHealth(*targetActor, *canonicalTarget);
        targetActor->hasStatsDynamicData = true;

        BaseActorList statsList;
        statsList.guid = player.guid;
        statsList.cell = player.cell;
        statsList.authorityLeaseId = targetCell->getAuthorityLeaseId();
        statsList.baseActors.push_back(*targetActor);
        statsList.count = 1;
        ActorPacket* statsPacket = actorPacketController->GetPacket(ID_ACTOR_STATS_DYNAMIC);
        statsPacket->setActorList(&statsList);
        statsPacket->Send(player.guid);
        targetCell->sendToLoaded(statsPacket, &statsList);

        if (result.targetDied)
        {
            BaseActorList deathList = statsList;
            deathList.baseActors.front().killer.isPlayer = true;
            deathList.baseActors.front().killer.guid = player.guid;
            ActorPacket* deathPacket = actorPacketController->GetPacket(ID_ACTOR_DEATH);
            deathPacket->setActorList(&deathList);
            deathPacket->Send(player.guid);
            targetCell->sendToLoaded(deathPacket, &deathList);
            baseActorList = deathList;
            Script::Call<Script::CallbackIdentity("OnActorDeath")>(
                player.getId(), player.cell.getShortDescription().c_str());
        }
    }
    return true;
}

bool Networking::validateActorAttacks(Player& player, const BaseActorList& incoming)
{
    bool valid = !incoming.cell.getShortDescription().empty()
        && incoming.count == incoming.baseActors.size();
    for (const BaseActor& actor : incoming.baseActors)
    {
        const Attack& attack = actor.attack;
        valid = valid && (actor.refNum != 0 || actor.mpNum != 0)
            && !(actor.refNum != 0 && actor.mpNum != 0)
            && (attack.type == Attack::MELEE || attack.type == Attack::RANGED)
            && attack.attackAnimation.size() <= 128
            && attack.rangedWeaponId.size() <= 256
            && attack.rangedAmmoId.size() <= 256;
        if (attack.type == Attack::RANGED)
        {
            valid = valid && std::isfinite(attack.attackStrength)
                && attack.attackStrength >= 0 && attack.attackStrength <= 1;
            for (const float coordinate : attack.projectileOrigin.origin)
                valid = valid && std::isfinite(coordinate);
            for (const float coordinate : attack.projectileOrigin.orientation)
                valid = valid && std::isfinite(coordinate);
        }
        if (!attack.pressed)
        {
            if (attack.target.isPlayer)
                valid = valid && attack.target.guid.g != 0;
            else
                valid = valid && (attack.target.refNum != 0 || attack.target.mpNum != 0)
                    && !(attack.target.refNum != 0 && attack.target.mpNum != 0);
        }
    }
    if (valid)
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid actor attack list from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.g), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated invalid actor attack intents");
    return false;
}

void Networking::sanitizeActorAttack(BaseActor& actor) noexcept
{
    actor.attack.success = false;
    actor.attack.isHit = false;
    actor.attack.damage = 0;
    actor.attack.block = false;
    actor.attack.knockdown = false;
    actor.attack.applyWeaponEnchantment = false;
    actor.attack.applyAmmoEnchantment = false;
}

bool Networking::resolveActorAttack(Player& player, BaseActorList& actorList,
    std::size_t actorIndex, std::optional<BaseActor>& actorDeath,
    std::string& rejectionReason)
{
    actorDeath.reset();
    rejectionReason.clear();
    if (actorIndex >= actorList.baseActors.size())
    {
        rejectionReason = "the actor attack index is invalid";
        return false;
    }

    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    if (serverCell == nullptr || *serverCell->getAuthority() != player.guid
        || serverCell->getAuthorityLeaseId() != actorList.authorityLeaseId)
    {
        rejectionReason = "the actor authority lease is no longer valid";
        return false;
    }

    BaseActor& submittedActor = actorList.baseActors[actorIndex];
    BaseActor* attackerActor = serverCell->getActor(
        submittedActor.refNum, submittedActor.mpNum);
    if (attackerActor == nullptr)
    {
        rejectionReason = "the attacking actor is absent from canonical cell state";
        return false;
    }

    const mechanics::CombatantId attackerId = actorCombatantId(
        actorList.cell, *attackerActor);
    auto attackerState = mCombatResolver.find(attackerId);
    if (!attackerState && attackerActor->hasStatsDynamicData)
    {
        const mechanics::CombatantState initial = actorCombatState(
            *attackerActor, attackerActor, std::nullopt, true);
        if (mCombatResolver.upsert(attackerId, initial))
            attackerState = initial;
    }
    if (!attackerState)
    {
        rejectionReason = "the attacking actor has no canonical combat state";
        return false;
    }
    attackerState->position = { attackerActor->position.pos[0],
        attackerActor->position.pos[1], attackerActor->position.pos[2] };
    if (!mCombatResolver.upsert(attackerId, *attackerState))
    {
        rejectionReason = "the attacking actor canonical state is invalid";
        return false;
    }

    mechanics::CombatantId targetId;
    Player* targetPlayer = nullptr;
    BaseActor* targetActor = nullptr;
    if (submittedActor.attack.target.isPlayer)
    {
        targetPlayer = Players::getPlayer(submittedActor.attack.target.guid);
        if (targetPlayer == nullptr
            || !mAuthenticatedConnections.contains(targetPlayer->guid.g)
            || targetPlayer->cell.getShortDescription()
                != actorList.cell.getShortDescription())
        {
            rejectionReason = "the target player is unavailable or in another cell";
            return false;
        }
        targetId = { mechanics::CombatantKind::Player, targetPlayer->guid.g, {} };
        auto state = mCombatResolver.find(targetId);
        if (!state)
        {
            rejectionReason = "the target player has no canonical combat state";
            return false;
        }
        state->position = { targetPlayer->position.pos[0], targetPlayer->position.pos[1],
            targetPlayer->position.pos[2] };
        if (!mCombatResolver.upsert(targetId, *state))
        {
            rejectionReason = "the target player canonical state is invalid";
            return false;
        }
    }
    else
    {
        targetActor = serverCell->getActor(submittedActor.attack.target.refNum,
            submittedActor.attack.target.mpNum);
        if (targetActor == nullptr)
        {
            rejectionReason = "the target actor is absent from canonical cell state";
            return false;
        }
        targetId = actorCombatantId(actorList.cell, *targetActor);
        auto state = mCombatResolver.find(targetId);
        if (!state && targetActor->hasStatsDynamicData)
        {
            const mechanics::CombatantState initial = actorCombatState(
                *targetActor, targetActor, std::nullopt, true);
            if (mCombatResolver.upsert(targetId, initial))
                state = initial;
        }
        if (!state)
        {
            rejectionReason = "the target actor has no canonical combat state";
            return false;
        }
        state->position = { targetActor->position.pos[0], targetActor->position.pos[1],
            targetActor->position.pos[2] };
        if (!mCombatResolver.upsert(targetId, *state))
        {
            rejectionReason = "the target actor canonical state is invalid";
            return false;
        }
    }

    const double strength = submittedActor.attack.type == Attack::RANGED
        ? static_cast<double>(submittedActor.attack.attackStrength) : 1.0;
    if (!std::isfinite(strength) || strength < 0 || strength > 1)
    {
        rejectionReason = "the script-modified actor attack strength is invalid";
        return false;
    }

    const mechanics::AttackIntent intent{
        attackerId, targetId, mCurrentApplicationSequence,
        submittedActor.attack.type == Attack::RANGED
            ? mechanics::AttackKind::Ranged : mechanics::AttackKind::Melee,
        strength };
    constexpr double randomScale = 1.0 / 4294967296.0;
    const mechanics::CombatResult result = mCombatResolver.resolve(
        intent, static_cast<double>(randombytes_random()) * randomScale);
    if (!result.applied())
    {
        rejectionReason = mechanics::describe(result.decision);
        return false;
    }

    submittedActor.attack.success
        = result.decision == mechanics::CombatDecision::AppliedHit;
    submittedActor.attack.isHit = submittedActor.attack.success;
    submittedActor.attack.damage = static_cast<float>(result.damage);

    const auto canonicalTarget = mCombatResolver.find(targetId);
    if (!canonicalTarget)
    {
        rejectionReason = "the canonical target disappeared after combat resolution";
        return false;
    }

    if (targetPlayer != nullptr)
    {
        applyCanonicalHealth(*targetPlayer, *canonicalTarget);
        targetPlayer->exchangeFullInfo = false;
        targetPlayer->statsDynamicIndexChanges.clear();
        targetPlayer->statsDynamicIndexChanges.push_back(0);
        PlayerPacket* statsPacket = playerPacketController->GetPacket(
            ID_PLAYER_STATS_DYNAMIC);
        statsPacket->setPlayer(targetPlayer);
        statsPacket->Send(targetPlayer->guid);
        targetPlayer->sendToLoaded(statsPacket);
        if (result.targetDied)
        {
            Target killer;
            killer.refId = attackerActor->refId;
            killer.refNum = attackerActor->refNum;
            killer.mpNum = attackerActor->mpNum;
            killer.name = attackerActor->refId;
            publishCanonicalPlayerDeath(*targetPlayer, killer);
        }
    }
    else
    {
        applyCanonicalHealth(*targetActor, *canonicalTarget);
        targetActor->hasStatsDynamicData = true;

        BaseActorList statsList;
        statsList.guid = player.guid;
        statsList.cell = actorList.cell;
        statsList.authorityLeaseId = serverCell->getAuthorityLeaseId();
        statsList.baseActors.push_back(*targetActor);
        statsList.count = 1;
        ActorPacket* statsPacket = actorPacketController->GetPacket(
            ID_ACTOR_STATS_DYNAMIC);
        statsPacket->setActorList(&statsList);
        statsPacket->Send(player.guid);
        serverCell->sendToLoaded(statsPacket, &statsList);

        if (result.targetDied)
            actorDeath = *targetActor;
    }
    return true;
}

void Networking::rejectActorDeathClaims(Player& player,
    const BaseActorList& incoming)
{
    bool duplicateCanonicalDeath = !incoming.baseActors.empty();
    for (const BaseActor& actor : incoming.baseActors)
    {
        const mechanics::CombatantId id = actorCombatantId(incoming.cell, actor);
        const auto state = mCombatResolver.find(id);
        if (!state || state->alive || state->health > 0)
        {
            duplicateCanonicalDeath = false;
            break;
        }
    }
    if (duplicateCanonicalDeath)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
            "Ignored duplicate canonical actor death acknowledgement from connection %llu",
            static_cast<unsigned long long>(player.guid.g));
        return;
    }

    const unsigned int violations = ++mCombatViolations[player.guid.g];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected client-claimed actor death from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.g), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.g }, "repeated client-claimed actor deaths");
}

persistence::QueueDecision Networking::queuePersistenceWrite(
    std::filesystem::path path, std::string_view contents)
{
    persistence::AtomicWriteOptions options;
    options.backup = persistence::BackupPolicy::MaintainOne;
    options.maximumBytes = 64U * 1024U * 1024U;
    const auto bytes = std::as_bytes(std::span(contents));
    const std::string displayPath = path.generic_string();
    return mPersistenceService.save(std::move(path), bytes, std::move(options),
        [displayPath](const persistence::PersistenceResult& result) {
            if (!result.success)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                    "Asynchronous persistence failed for %s: %s",
                    displayPath.c_str(), result.error.c_str());
            }
        });
}

void Networking::flushPersistence()
{
    mPersistenceService.flush();
}

bool Networking::isPassworded() const
{
    return mAuthentication.requiresAccessPassword();
}

void Networking::processSystemPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player == nullptr)
        return;
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected deprecated system packet %u after the protocol-11 cutover",
        static_cast<unsigned int>(packet->data[0]));
    kickPlayer(player->guid);
}

void Networking::processPlayerPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player == nullptr)
        return;

    PlayerPacket *myPacket = playerPacketController->GetPacket(packet->data[0]);

    if (!player->isHandshaked())
    {
        player->incrementHandshakeAttempts();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Have not completed handshake with client at %s", packet->systemAddress.ToString());
        LOG_APPEND(TimedLog::LOG_WARN, "- Attempts so far: %i", player->getHandshakeAttempts());

        if (player->getHandshakeAttempts() > 20)
            kickPlayer(player->guid, false);
        else if (player->getHandshakeAttempts() > 5)
            kickPlayer(player->guid, true);

        return;
    }

    if (packet->data[0] == ID_LOADED)
        player->setLoadState(Player::LOADED);
    else if (packet->data[0] == ID_PLAYER_BASEINFO)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_BASEINFO about %s", player->npc.mName.c_str());

        BasePlayer validation(packet->guid);
        myPacket->setPlayer(&validation);
        myPacket->Read();
        if (!myPacket->isPacketValid())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Invalid ID_PLAYER_BASEINFO packet from client at %s",
                packet->systemAddress.ToString());
            kickPlayer(player->guid);
            return;
        }
        myPacket->setPlayer(player);
        myPacket->Read();
        if (!myPacket->isPacketValid())
        {
            kickPlayer(player->guid);
            return;
        }
        myPacket->Send(true);
    }

    if (player->getLoadState() == Player::NOTLOADED)
        return;
    else if (player->getLoadState() == Player::LOADED)
    {
        player->setLoadState(Player::POSTLOADED);
        newPlayer(packet->guid);
        return;
    }


    if (!PlayerProcessor::Process(*packet))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processActorPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player == nullptr)
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ActorProcessor::Process(*packet, baseActorList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processObjectPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player == nullptr)
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ObjectProcessor::Process(*packet, baseObjectList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ObjectPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processWorldstatePacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player == nullptr)
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!WorldstateProcessor::Process(*packet, baseWorldstate))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled WorldstatePacket with identifier %i has arrived", packet->data[0]);

}

bool Networking::preInit(RakNet::Packet *packet, RakNet::BitStream &bsIn)
{
    if (packet->data[0] != ID_GAME_PREINIT)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Connection %llu sent the wrong first application packet",
            static_cast<unsigned long long>(packet->guid.g));
        mEndpoint.disconnect({ packet->guid.g });
        return false;
    }

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received content manifest from connection %llu",
        static_cast<unsigned long long>(packet->guid.g));
    PacketPreInit::PluginContainer dataFiles;

    PacketPreInit packetPreInit(peer);
    packetPreInit.SetReadStream(&bsIn);
    packetPreInit.setChecksums(&dataFiles);
    packetPreInit.Read();

    if (!packetPreInit.isPacketValid() || dataFiles.empty())
    {
        LOG_APPEND(TimedLog::LOG_ERROR, "- Packet was invalid");
        mEndpoint.disconnect({ packet->guid.g });
        return false;
    }

    auto dataFile = dataFiles.begin();
    if (samples.size() == dataFiles.size())
    {
        for (int i = 0; dataFile != dataFiles.end(); dataFile++, i++)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- idx: %i\tchecksum: %X\tfile: %s", i, dataFile->second[0], dataFile->first.c_str());
            // Check if the filenames match, ignoring case
            if (Misc::StringUtils::ciEqual(samples[i].first, dataFile->first))
            {
                auto &hashList = samples[i].second;
                // Proceed if no checksums have been listed for this dataFile on the server
                if (hashList.empty())
                    continue;
                auto it = find(hashList.begin(), hashList.end(), dataFile->second[0]);
                // Break the loop if the client's checksum isn't among those accepted by
                // the server
                if (it == hashList.end())
                    break;
            }
            else // name is incorrect
                break;
        }
    }
    RakNet::BitStream bs;
    packetPreInit.SetSendStream(&bs);
    packetPreInit.setGUID(packet->guid);

    // If the loop above was broken, then the client's data files do not match the server's
    if (dataFileEnforcementState && dataFile != dataFiles.end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Client was not allowed to connect due to incompatible data files");
        packetPreInit.setChecksums(&samples);
        packetPreInit.Send(packet->guid);
        mEndpoint.disconnect({ packet->guid.g });
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Client was allowed to connect");
        PacketPreInit::PluginContainer tmp;
        packetPreInit.setChecksums(&tmp);
        packetPreInit.Send(packet->guid);
        return true;
    }

    return false;
}

void Networking::update(RakNet::Packet *packet, RakNet::BitStream &bsIn)
{
    if (systemPacketController->ContainsPacket(packet->data[0]))
    {
        systemPacketController->SetStream(&bsIn, nullptr);
        processSystemPacket(packet);
    }
    else if (playerPacketController->ContainsPacket(packet->data[0]))
    {
        playerPacketController->SetStream(&bsIn, nullptr);
        processPlayerPacket(packet);
    }
    else if (actorPacketController->ContainsPacket(packet->data[0]))
    {
        actorPacketController->SetStream(&bsIn, 0);
        processActorPacket(packet);
    }
    else if (objectPacketController->ContainsPacket(packet->data[0]))
    {
        objectPacketController->SetStream(&bsIn, 0);
        processObjectPacket(packet);
    }
    else if (worldstatePacketController->ContainsPacket(packet->data[0]))
    {
        worldstatePacketController->SetStream(&bsIn, 0);
        processWorldstatePacket(packet);
    }
    else
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled RakNet packet with identifier %i has arrived", packet->data[0]);
}

void Networking::newPlayer(RakNet::RakNetGUID guid)
{
    playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_POSITION)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->RequestData(guid);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Sending info about other players to %lu", guid.g);

    for (const auto& [playerGuid, ownedPlayer] : *players) //sending other players to new player
    {
        // If we are iterating over the new player, don't send the packets below
        if (playerGuid == guid) continue;

        // If an invalid key makes it into the Players map, ignore it
        else if (playerGuid == RakNet::UNASSIGNED_CRABNET_GUID) continue;

        // if player not fully connected
        else if (!ownedPlayer) continue;

        // If we are iterating over a player who has inputted their name, proceed
        else if (ownedPlayer->getLoadState() == Player::POSTLOADED)
        {
            Player* otherPlayer = ownedPlayer.get();
            playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->setPlayer(otherPlayer);
            playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(otherPlayer);
            playerPacketController->GetPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(otherPlayer);
            playerPacketController->GetPacket(ID_PLAYER_SKILL)->setPlayer(otherPlayer);
            playerPacketController->GetPacket(ID_PLAYER_POSITION)->setPlayer(otherPlayer);
            playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(otherPlayer);
            playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->setPlayer(otherPlayer);

            playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_ATTRIBUTE)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_SKILL)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_POSITION)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->Send(guid);
        }
    }

    LOG_APPEND(TimedLog::LOG_WARN, "- Done");

}

void Networking::disconnectPlayer(RakNet::RakNetGUID guid)
{
    Player *player = Players::getPlayer(guid);
    if (!player)
        return;
    if (mAuthenticatedConnections.contains(guid.g))
    {
        Script::Call<Script::CallbackIdentity("OnPlayerDisconnect")>(player->getId());
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(player);
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(true);
    }
    mAuthorityLeases.releaseOwner(guid.g);
    mAuthorityViolations.erase(guid.g);
    resetPlayerMovement(guid.g);
    mPlayerLifecycle.erase(guid.g);
    mLifecycleViolations.erase(guid.g);
    mInventoryLedger.erase({ mechanics::InventoryOwnerKind::Player, guid.g });
    mInventoryViolations.erase(guid.g);
    mCombatResolver.erase({ mechanics::CombatantKind::Player, guid.g, {} });
    mCombatViolations.erase(guid.g);
    mActiveEffectLedger.erase({ mechanics::CombatantKind::Player, guid.g, {} });
    mActiveEffectViolations.erase(guid.g);
    mCastViolations.erase(guid.g);
    mAcceptedPlayerActiveEffectIntents.erase(guid.g);
    mRelayedPlayerActiveEffectIntents.erase(guid.g);
    mAcceptedActorActiveEffectIntents.erase(guid.g);
    mRelayedActorActiveEffectIntents.erase(guid.g);
    Players::deletePlayer(guid);
}

PlayerPacketController *Networking::getPlayerPacketController() const
{
    return playerPacketController.get();
}

ActorPacketController *Networking::getActorPacketController() const
{
    return actorPacketController.get();
}

ObjectPacketController *Networking::getObjectPacketController() const
{
    return objectPacketController.get();
}

WorldstatePacketController *Networking::getWorldstatePacketController() const
{
    return worldstatePacketController.get();
}

BaseActorList *Networking::getReceivedActorList()
{
    return &baseActorList;
}

BaseObjectList *Networking::getReceivedObjectList()
{
    return &baseObjectList;
}

BaseWorldstate *Networking::getReceivedWorldstate()
{
    return &baseWorldstate;
}

int Networking::getCurrentMpNum()
{
    return currentMpNum;
}

void Networking::setCurrentMpNum(int value)
{
    currentMpNum = value;
}

int Networking::incrementMpNum()
{
    currentMpNum++;
    Script::Call<Script::CallbackIdentity("OnMpNumIncrement")>(currentMpNum);
    return currentMpNum;
}

bool Networking::getDataFileEnforcementState()
{
    return dataFileEnforcementState;
}

void Networking::setDataFileEnforcementState(bool state)
{
    dataFileEnforcementState = state;
}

bool Networking::getScriptErrorIgnoringState()
{
    return scriptErrorIgnoringState;
}

void Networking::setScriptErrorIgnoringState(bool state)
{
    scriptErrorIgnoringState = state;
}

const Networking &Networking::get()
{
    return *sThis;
}


Networking *Networking::getPtr()
{
    return sThis;
}

std::string Networking::getPeerAddress(RakNet::RakNetGUID guid) const
{
    return mEndpoint.peerAddress({ guid.g }).value_or(std::string{});
}

void Networking::stopServer(int code)
{
    running = false;
    exitCode = code;
}

void signalHandler(int signum) 
{
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    //15 is SIGTERM(Normal OS stop call), 2 is SIGINT(Ctrl+C)
    if(signum == 15 || signum == 2)
    {
        killLoop = true;
    }
}

int Networking::mainLoop()
{
#ifndef _WIN32
    struct sigaction sigIntHandler;
    
    sigIntHandler.sa_handler = signalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
#endif
    
    while (running && !killLoop)
    {
#ifndef _WIN32
        sigaction(SIGTERM, &sigIntHandler, NULL);
        sigaction(SIGINT, &sigIntHandler, NULL);
#endif
        if (kbhit() && getch() == '\n')
            break;
        if (auto event = mEndpoint.poll(std::chrono::milliseconds(1)))
            processTransportEvent(std::move(*event));
        TimerAPI::Tick();
    }

    TimerAPI::Terminate();
    return exitCode;
}

void Networking::processTransportEvent(transport::TransportEvent event)
{
    const RakNet::RakNetGUID guid(event.connection.value);
    switch (event.type)
    {
        case transport::TransportEventType::Connected:
        {
            const std::string peerAddress = mEndpoint.peerAddress(event.connection).value_or(std::string{});
            if (mBannedAddresses.contains(peerAddress))
            {
                disconnectTransport(event.connection, "peer address is banned");
                return;
            }
            if (!mDispatcher.addConnection(event.connection))
            {
                disconnectTransport(event.connection, "server connection capacity reached");
                return;
            }
            const Players::CreationResult creation = Players::newPlayer(guid, mMaximumConnections);
            if (!creation)
            {
                mDispatcher.removeConnection(event.connection);
                disconnectTransport(event.connection,
                    creation.status == Players::CreationStatus::AlreadyExists
                        ? "a player already exists for this connection"
                        : "no free player slot is available");
                return;
            }
            if (Player* player = creation.player)
            {
                try
                {
                    Script::Call<Script::CallbackIdentity("OnTransportConnect")>(player->getId());
                }
                catch (...)
                {
                    disconnectTransport(event.connection,
                        "OnTransportConnect rejected the connection");
                    return;
                }
            }
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Authenticated transport connection %llu established",
                static_cast<unsigned long long>(event.connection.value));
            break;
        }
        case transport::TransportEventType::Disconnected:
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Transport connection %llu disconnected: %s",
                static_cast<unsigned long long>(event.connection.value),
                event.detail.c_str());
            disconnectPlayer(guid);
            mAuthenticatedConnections.erase(event.connection.value);
            mReceiver.removeConnection(event.connection);
            mDispatcher.removeConnection(event.connection);
            break;
        case transport::TransportEventType::Message:
            if (event.message.messageType
                    == static_cast<std::uint16_t>(protocol::MessageType::AccountLogin)
                || event.message.messageType
                    == static_cast<std::uint16_t>(protocol::MessageType::AccountRegister))
                processAuthenticationMessage(std::move(event.message));
            else
                processApplicationMessage(std::move(event.message));
            break;
        case transport::TransportEventType::TrustRequired:
            disconnectTransport(event.connection,
                "a server endpoint cannot request trust confirmation");
            break;
    }
}

void Networking::processApplicationMessage(transport::TransportMessage message)
{
    transport::ReceivedApplicationPacket application;
    const auto received = mReceiver.receive(message, application);
    if (received.status == transport::ApplicationReceiveStatus::StaleSnapshot)
        return;
    if (!received)
    {
        disconnectTransport(message.connection, "invalid protocol-11 application packet");
        return;
    }

    std::vector<unsigned char> frame;
    protocol::CodecError codecError = protocol::CodecError::None;
    if (!transport::buildLegacyPacketFrame(application, frame, codecError))
    {
        disconnectTransport(message.connection, "failed to adapt application packet");
        return;
    }

    RakNet::Packet packet{};
    packet.data = frame.data();
    packet.length = static_cast<unsigned int>(frame.size());
    packet.guid = RakNet::RakNetGUID(message.connection.value);
    RakNet::BitStream stream(&packet.data[1], packet.length - 1, false);
    stream.IgnoreBytes(static_cast<unsigned int>(RakNet::RakNetGUID::size()));

    const auto state = mEndpoint.state(message.connection);
    if (state == session::State::TransportAuthenticated)
    {
        if (!preInit(&packet, stream))
            return;
        transport::TransportError error;
        if (mEndpoint.advance(message.connection, session::State::ContentVerified, error)
            != session::TransitionResult::Advanced)
            disconnectTransport(message.connection, "content session transition failed");
        return;
    }

    if (application.id == protocol::ApplicationPacketId::Loaded
        && state == session::State::AccountAuthenticated)
    {
        Player* player = Players::getPlayer(packet.guid);
        if (player == nullptr)
        {
            disconnectTransport(message.connection, "spawn requested without a player slot");
            return;
        }
        PlayerPacket* response = playerPacketController->GetPacket(ID_LOADED);
        response->setPlayer(player);
        if (response->Send(packet.guid) == 0)
        {
            disconnectTransport(message.connection, "failed to send spawn result");
            return;
        }
        transport::TransportError error;
        if (mEndpoint.advance(message.connection, session::State::Spawned, error)
            != session::TransitionResult::Advanced)
        {
            disconnectTransport(message.connection, "spawn session transition failed");
            return;
        }
        try
        {
            Script::Call<Script::CallbackIdentity("OnPlayerConnect")>(player->getId());
        }
        catch (...)
        {
            disconnectTransport(message.connection, "OnPlayerConnect failed");
            return;
        }
    }
    mCurrentApplicationSequence = application.sequence;
    update(&packet, stream);
}

void Networking::processAuthenticationMessage(transport::TransportMessage message)
{
    security::AuthenticationRequest request;
    if (!security::decodeAuthenticationRequest(message.payload, request))
    {
        disconnectTransport(message.connection, "invalid authentication request");
        return;
    }
    const bool registration = request.operation == security::AuthenticationOperation::Register;
    const auto expectedType = registration ? protocol::MessageType::AccountRegister
                                           : protocol::MessageType::AccountLogin;
    if (message.messageType != static_cast<std::uint16_t>(expectedType))
    {
        disconnectTransport(message.connection, "authentication operation mismatch");
        return;
    }

    const std::string address = mEndpoint.peerAddress(message.connection)
        .value_or("connection-" + std::to_string(message.connection.value));
    auto result = mAuthentication.authenticate(std::move(request), address);
    if (!sendAuthenticationResponse(message.connection, result.response))
    {
        disconnectTransport(message.connection, "failed to send authentication result");
        return;
    }
    if (!result.response.authenticated())
        return;

    Player* player = Players::getPlayer(RakNet::RakNetGUID(message.connection.value));
    if (player == nullptr)
    {
        disconnectTransport(message.connection, "authenticated player slot was missing");
        return;
    }
    player->npc.mName = result.accountName;
    player->setHandshake();

    transport::TransportError error;
    if (mEndpoint.advance(message.connection, session::State::AccountAuthenticated, error)
        != session::TransitionResult::Advanced)
    {
        disconnectTransport(message.connection, "authentication session transition failed");
        return;
    }
    mAuthenticatedConnections.insert(message.connection.value);
    const unsigned short pid = player->getId();
    try
    {
        Script::Call<Script::CallbackIdentity("OnPlayerAuthenticated")>(
            pid, result.accountName.c_str(), result.isNewAccount);
    }
    catch (...)
    {
        disconnectTransport(message.connection, "OnPlayerAuthenticated failed");
    }
}

bool Networking::sendAuthenticationResponse(transport::TransportConnectionId connection,
    const security::AuthenticationResponse& response)
{
    std::vector<std::byte> payload;
    protocol::CodecError codecError = protocol::CodecError::None;
    if (!security::encodeAuthenticationResponse(response, payload, codecError))
        return false;
    transport::TransportMessage message;
    message.connection = connection;
    message.delivery = transport::DeliveryMode::ReliableOrdered;
    message.lane = transport::MessageLane::System;
    message.messageType
        = static_cast<std::uint16_t>(protocol::MessageType::AuthenticationResult);
    message.subject = connection.value;
    message.sequence = 1;
    message.payload = std::move(payload);
    transport::TransportError error;
    return mEndpoint.send(std::move(message), error);
}

void Networking::disconnectTransport(
    transport::TransportConnectionId connection, const char* reason)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Disconnecting transport connection %llu: %s",
        static_cast<unsigned long long>(connection.value), reason);
    mEndpoint.disconnect(connection);
}

void Networking::kickPlayer(RakNet::RakNetGUID guid, bool sendNotification)
{
    (void)sendNotification;
    disconnectTransport({ guid.g }, "kicked by server");
}

void Networking::banAddress(const char *ipAddress)
{
    if (ipAddress != nullptr && *ipAddress != '\0')
        mBannedAddresses.emplace(ipAddress);
}

void Networking::unbanAddress(const char *ipAddress)
{
    if (ipAddress != nullptr)
        mBannedAddresses.erase(ipAddress);
}

unsigned short Networking::numberOfConnections() const
{
    return static_cast<unsigned short>(mDispatcher.connectionCount());
}

unsigned int Networking::maxConnections() const
{
    return mMaximumConnections;
}

int Networking::getAvgPing(RakNet::AddressOrGUID addr) const
{
    (void)addr;
    return -1;
}

unsigned short Networking::getPort() const
{
    return mPort;
}

void Networking::postInit()
{
    Script::Call<Script::CallbackIdentity("OnRequestDataFileList")>();
    Script::Call<Script::CallbackIdentity("OnServerPostInit")>();
}

PacketPreInit::PluginContainer &Networking::getSamples()
{
    return samples;
}
