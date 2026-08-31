#include "Player.hpp"
#include "processors/ProcessorInitializer.hpp"

#include <components/misc/stringops.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Metrics/ProcessMemory.hpp>
#include <components/openmw-mp/Security/AuthenticationMessages.hpp>
#include <components/openmw-mp/Security/PasswordHash.hpp>
#include <components/openmw-mp/Session/SessionState.hpp>

#include <sodium.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <Script/Script.hpp>
#include <Script/API/TimerAPI.hpp>
#include <chrono>
#include <thread>
#include <csignal>
#include <utility>

#ifdef _WIN32
#include <conio.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

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

namespace
{
    double dynamicMaximum(const ESM::StatState<float>& stat) noexcept;
    mwmp::mechanics::CombatantState actorCombatState(
        const mwmp::BaseActor& actor, const mwmp::BaseActor* cachedActor,
        const std::optional<mwmp::mechanics::CombatantState>& existing,
        bool replaceResources);
    mwmp::mechanics::CombatantId actorCombatantId(
        const ESM::Cell& cell, const mwmp::BaseActor& actor);
    void applyCanonicalHealth(
        Player& player, const mwmp::mechanics::CombatantState& state) noexcept;
    void applyCanonicalHealth(mwmp::BaseActor& actor,
        const mwmp::mechanics::CombatantState& state) noexcept;
    void applyCanonicalMagicka(Player& player,
        const mwmp::mechanics::SpellCombatantState& state) noexcept;
    void applyCanonicalMagicka(mwmp::BaseActor& actor,
        const mwmp::mechanics::SpellCombatantState& state) noexcept;
    void applyCanonicalFatigue(
        Player& player, const mwmp::mechanics::CombatantState& state) noexcept;
    void applyCanonicalFatigue(mwmp::BaseActor& actor,
        const mwmp::mechanics::CombatantState& state) noexcept;

    bool stdinHasInput() noexcept
    {
#ifdef _WIN32
        return _kbhit() != 0;
#else
        pollfd descriptor{ STDIN_FILENO, POLLIN, 0 };
        return poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0;
#endif
    }

    int readStdinCharacter()
    {
#ifdef _WIN32
        return _getch();
#else
        return std::cin.get();
#endif
    }
}

Networking::Networking(transport::Protocol11Endpoint& endpoint,
    const std::filesystem::path& credentialDirectory,
    const std::filesystem::path& legacyPlayerDirectory,
    unsigned int maximumConnections, unsigned short port,
    double movementMaximumSpeed, unsigned int movementViolationLimit)
    : mEndpoint(endpoint)
    , mDispatcher(endpoint.transport(), transport::ApplicationPacketFlow::ServerToClient,
        maximumConnections, &mMetrics)
    , mReceiver(transport::ApplicationPacketFlow::ClientToServer)
    , mAuthentication(credentialDirectory, legacyPlayerDirectory)
    , mMovementValidator(maximumConnections)
    , mPlayerLifecycle(maximumConnections)
    , mInventoryLedger(mechanics::ActorMagicRegistry::MaximumActors
        + maximumConnections * 2U)
    , mProgressionLedger(maximumConnections)
    , mShapeshiftLedger(maximumConnections)
    , mSpellbookLedger(maximumConnections)
    , mMaximumConnections(maximumConnections)
    , mPort(port)
    , mMovementMaximumSpeed(movementMaximumSpeed)
    , mMovementViolationLimit(movementViolationLimit)
{
    sThis = this;
    players = Players::getPlayers();

    CellController::create();

    systemPacketController = std::make_unique<SystemPacketController>();
    playerPacketController = std::make_unique<PlayerPacketController>();
    actorPacketController = std::make_unique<ActorPacketController>();
    objectPacketController = std::make_unique<ObjectPacketController>();
    worldstatePacketController = std::make_unique<WorldstatePacketController>();

    // Set send stream
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
    const ESM::Cell& cell, mwmp::transport::TransportConnectionId owner)
{
    if (!mAuthenticatedConnections.contains(owner.value))
        return std::nullopt;

    const std::string cellDescription = cell.getShortDescription();
    const auto now = session::AuthorityLeaseManager::Clock::now();
    if (const auto current = mAuthorityLeases.find(cellDescription); current)
    {
        if (current->owner == owner.value
            && mAuthorityLeases.renew(current->cell, current->owner, current->leaseId, now)
                == session::LeaseValidation::Valid)
            return mAuthorityLeases.find(cellDescription);
        mAuthorityLeases.release(current->cell, current->owner, current->leaseId);
    }

    const session::LeaseGrantResult result = mAuthorityLeases.grant(
        cellDescription, owner.value, now);
    if (!result.lease || (result.decision != session::LeaseGrantDecision::Granted
            && result.decision != session::LeaseGrantDecision::Existing))
        return std::nullopt;
    return result.lease;
}

bool Networking::validateActorAuthority(const BaseActorList& actorList)
{
    const auto validation = mAuthorityLeases.validateAndRenew(
        actorList.cell.getShortDescription(), actorList.guid.value, actorList.authorityLeaseId,
        session::AuthorityLeaseManager::Clock::now());
    if (validation == session::LeaseValidation::Valid)
        return true;

    const unsigned int violations = ++mAuthorityViolations[actorList.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor simulation update from connection %llu for cell %s: %s (violation %u)",
        static_cast<unsigned long long>(actorList.guid.value),
        actorList.cell.getShortDescription().c_str(), session::describe(validation), violations);
    if (violations >= 5)
        disconnectTransport({ actorList.guid.value }, "repeated invalid actor authority leases");
    return false;
}

bool Networking::releaseActorAuthority(const ESM::Cell& cell, mwmp::transport::TransportConnectionId owner,
    std::uint64_t leaseId)
{
    return mAuthorityLeases.release(cell.getShortDescription(), owner.value, leaseId);
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
        player.guid.value, sample, mMovementMaximumSpeed,
        mechanics::MovementValidator::Clock::now());
    if (result.accepted())
        return true;

    const unsigned int violations = ++mMovementViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected movement from connection %llu: %s; distance %.3f, allowed %.3f (violation %u)",
        static_cast<unsigned long long>(player.guid.value), mechanics::describe(result.decision),
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
        disconnectTransport({ player.guid.value }, "repeated invalid movement samples");
    return false;
}

bool Networking::validatePlayerCellChange(Player& player,
    const BasePlayer& incoming)
{
    const mechanics::MovementValidationResult result
        = mMovementValidator.previewCellTransition(player.guid.value,
            incoming.cell.getShortDescription(),
            { incoming.previousCellPosition.pos[0],
                incoming.previousCellPosition.pos[1],
                incoming.previousCellPosition.pos[2] },
            128.0);
    if (result.accepted()
        && !mPendingPlayerCellChanges.contains(player.guid.value))
    {
        mPendingPlayerCellChanges.emplace(player.guid.value,
            PendingPlayerCellChange{ player.cell, player.previousCellPosition,
                player.isChangingRegion });
        return true;
    }

    const unsigned int violations = ++mMovementViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected cell transition intent from connection %llu to %s: %s; distance %.3f, allowed %.3f (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        incoming.cell.getShortDescription().c_str(),
        mPendingPlayerCellChanges.contains(player.guid.value)
            ? "another cell transition is pending"
            : mechanics::describe(result.decision),
        result.distance, result.allowedDistance, violations);
    if (violations >= mMovementViolationLimit)
        disconnectTransport({ player.guid.value },
            "repeated invalid cell transition intents");
    return false;
}

bool Networking::commitPlayerCellChange(Player& player)
{
    const auto pending = mPendingPlayerCellChanges.find(player.guid.value);
    if (pending == mPendingPlayerCellChanges.end())
        return false;

    const mechanics::MovementValidationResult result
        = mMovementValidator.acceptCellTransition(player.guid.value,
            player.cell.getShortDescription(),
            { player.previousCellPosition.pos[0],
                player.previousCellPosition.pos[1],
                player.previousCellPosition.pos[2] },
            128.0, mechanics::MovementValidator::Clock::now());
    if (result.accepted())
    {
        mPendingPlayerCellChanges.erase(pending);
        return true;
    }

    const unsigned int violations = ++mMovementViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified cell transition from connection %llu to %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        player.cell.getShortDescription().c_str(),
        mechanics::describe(result.decision), violations);
    if (violations >= mMovementViolationLimit)
        disconnectTransport({ player.guid.value },
            "repeated invalid cell transition intents");
    return false;
}

void Networking::cancelPlayerCellChange(Player& player) noexcept
{
    const auto pending = mPendingPlayerCellChanges.find(player.guid.value);
    if (pending == mPendingPlayerCellChanges.end())
        return;
    player.cell = pending->second.cell;
    player.previousCellPosition = pending->second.previousCellPosition;
    player.isChangingRegion = pending->second.isChangingRegion;
    mPendingPlayerCellChanges.erase(pending);
}

bool Networking::authorizePlayerMovement(const Player& player, double tolerance)
{
    return mMovementValidator.authorizeTransition(player.guid.value,
        player.cell.getShortDescription(),
        { player.position.pos[0], player.position.pos[1], player.position.pos[2] },
        tolerance, mechanics::MovementValidator::Clock::now());
}

void Networking::resetPlayerMovement(std::uint64_t connection) noexcept
{
    mMovementValidator.erase(connection);
    mMovementViolations.erase(connection);
    mPendingPlayerCellChanges.erase(connection);
}

bool Networking::acceptPlayerDeath(Player& player)
{
    const mechanics::CombatantId combatant{
        mechanics::CombatantKind::Player, player.guid.value, {} };
    const auto combatState = mCombatResolver.find(combatant);
    if (!combatState || combatState->alive || combatState->health > 0)
    {
        const unsigned int violations = ++mLifecycleViolations[player.guid.value];
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Rejected non-canonical death intent from connection %llu (violation %u)",
            static_cast<unsigned long long>(player.guid.value), violations);
        if (violations >= 5)
            disconnectTransport({ player.guid.value }, "repeated non-canonical death intents");
        return false;
    }

    const mechanics::PlayerLifeState lifeState
        = mPlayerLifecycle.state(player.guid.value);
    if (lifeState == mechanics::PlayerLifeState::Dead
        || lifeState == mechanics::PlayerLifeState::Respawning)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
            "Ignored duplicate death acknowledgement from connection %llu",
            static_cast<unsigned long long>(player.guid.value));
        return false;
    }

    const mechanics::PlayerLifeTransition transition
        = mPlayerLifecycle.reportDeath(player.guid.value);
    if (transition.applied())
        return true;

    const unsigned int violations = ++mLifecycleViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected death intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(transition.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid death intents");
    return false;
}

bool Networking::publishCanonicalPlayerDeath(Player& player, const Target& killer)
{
    const mechanics::PlayerLifeTransition transition
        = mPlayerLifecycle.reportDeath(player.guid.value);
    if (!transition.applied())
    {
        if (transition.decision == mechanics::PlayerLifeDecision::AlreadyDead)
            return false;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Failed to publish canonical death for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
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
        = mPlayerLifecycle.beginRespawn(player.guid.value, respawnType);
    if (transition.applied())
        return true;
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected server respawn transition for connection %llu: %s",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(transition.decision));
    return false;
}

bool Networking::acknowledgePlayerRespawn(Player& player, const BasePlayer& incoming)
{
    const mechanics::PlayerLifeTransition transition
        = mPlayerLifecycle.acknowledgeRespawn(player.guid.value, incoming.resurrectType);
    if (transition.applied())
        return true;

    const unsigned int violations = ++mLifecycleViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected respawn acknowledgement from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(transition.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid respawn acknowledgements");
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

    std::optional<mwmp::mechanics::SpellbookAction> spellbookAction(int action)
    {
        switch (action)
        {
            case mwmp::SpellbookChanges::SET:
                return mwmp::mechanics::SpellbookAction::Set;
            case mwmp::SpellbookChanges::ADD:
                return mwmp::mechanics::SpellbookAction::Add;
            case mwmp::SpellbookChanges::REMOVE:
                return mwmp::mechanics::SpellbookAction::Remove;
            default:
                return std::nullopt;
        }
    }

    std::vector<std::string> spellbookIds(const mwmp::SpellbookChanges& changes)
    {
        std::vector<std::string> result;
        result.reserve(changes.spells.size());
        for (const ESM::Spell& spell : changes.spells)
        {
            result.push_back(Misc::StringUtils::lowerCase(
                spell.mId.getRefIdString()));
        }
        return result;
    }

    std::vector<mwmp::mechanics::EquipmentChange> equipmentChanges(
        const mwmp::BasePlayer& player)
    {
        std::vector<mwmp::mechanics::EquipmentChange> result;
        if (player.exchangeFullInfo)
        {
            result.reserve(mwmp::mechanics::EquipmentLedger::SlotCount);
            for (std::size_t slot = 0;
                 slot < mwmp::mechanics::EquipmentLedger::SlotCount; ++slot)
            {
                const mwmp::Item& item = player.equipmentItems[slot];
                result.push_back({ slot,
                    { item.refId, item.count, item.charge,
                        item.enchantmentCharge } });
            }
            return result;
        }

        result.reserve(player.equipmentIndexChanges.size());
        for (const int slot : player.equipmentIndexChanges)
        {
            if (slot < 0
                || static_cast<std::size_t>(slot)
                    >= mwmp::mechanics::EquipmentLedger::SlotCount)
            {
                result.push_back(
                    { mwmp::mechanics::EquipmentLedger::SlotCount, {} });
                continue;
            }
            const mwmp::Item& item = player.equipmentItems[slot];
            result.push_back({ static_cast<std::size_t>(slot),
                { item.refId, item.count, item.charge,
                    item.enchantmentCharge } });
        }
        return result;
    }

    std::vector<mwmp::mechanics::ActorEquipmentUpdate> actorEquipmentUpdates(
        const mwmp::BaseActorList& actorList)
    {
        std::vector<mwmp::mechanics::ActorEquipmentUpdate> result;
        const std::string cell = actorList.cell.getShortDescription();
        result.reserve(actorList.baseActors.size());
        for (const mwmp::BaseActor& actor : actorList.baseActors)
        {
            mwmp::mechanics::ActorEquipmentUpdate update;
            update.identity = { cell, actor.refNum, actor.mpNum };
            for (std::size_t slot = 0;
                 slot < mwmp::mechanics::EquipmentLedger::SlotCount; ++slot)
            {
                const mwmp::Item& item = actor.equipmentItems[slot];
                update.equipment[slot] = { item.refId, item.count,
                    item.charge, item.enchantmentCharge };
            }
            result.push_back(std::move(update));
        }
        return result;
    }

    std::vector<mwmp::mechanics::ActorPositionUpdate> actorPositionUpdates(
        const mwmp::BaseActorList& actorList, std::uint64_t sequence)
    {
        std::vector<mwmp::mechanics::ActorPositionUpdate> result;
        const std::string cell = actorList.cell.getShortDescription();
        result.reserve(actorList.baseActors.size());
        for (const mwmp::BaseActor& actor : actorList.baseActors)
        {
            mwmp::mechanics::ActorPositionUpdate update;
            update.identity = { cell, actor.refNum, actor.mpNum };
            update.transform.position = { actor.position.pos[0], actor.position.pos[1],
                actor.position.pos[2] };
            update.transform.rotation = { actor.position.rot[0], actor.position.rot[1],
                actor.position.rot[2] };
            update.transform.direction = { actor.direction.pos[0], actor.direction.pos[1],
                actor.direction.pos[2] };
            update.transform.directionRotation = { actor.direction.rot[0],
                actor.direction.rot[1], actor.direction.rot[2] };
            update.sequence = sequence;
            result.push_back(std::move(update));
        }
        return result;
    }

    std::vector<mwmp::mechanics::ActorCellChangeUpdate> actorCellChangeUpdates(
        const mwmp::BaseActorList& actorList, std::uint64_t sequence)
    {
        std::vector<mwmp::mechanics::ActorCellChangeUpdate> result;
        const std::string sourceCell = actorList.cell.getShortDescription();
        result.reserve(actorList.baseActors.size());
        for (const mwmp::BaseActor& actor : actorList.baseActors)
        {
            mwmp::mechanics::ActorCellChangeUpdate update;
            update.source = { sourceCell, actor.refNum, actor.mpNum };
            update.destinationCell = actor.cell.getShortDescription();
            update.transform.position = { actor.position.pos[0], actor.position.pos[1],
                actor.position.pos[2] };
            update.transform.rotation = { actor.position.rot[0], actor.position.rot[1],
                actor.position.rot[2] };
            update.transform.direction = { actor.direction.pos[0], actor.direction.pos[1],
                actor.direction.pos[2] };
            update.transform.directionRotation = { actor.direction.rot[0],
                actor.direction.rot[1], actor.direction.rot[2] };
            update.sequence = sequence;
            result.push_back(std::move(update));
        }
        return result;
    }

    std::vector<mwmp::mechanics::ActorAiUpdate> actorAiUpdates(
        const mwmp::BaseActorList& actorList)
    {
        std::vector<mwmp::mechanics::ActorAiUpdate> result;
        const std::string cell = actorList.cell.getShortDescription();
        result.reserve(actorList.baseActors.size());
        for (const mwmp::BaseActor& actor : actorList.baseActors)
        {
            mwmp::mechanics::ActorAiUpdate update;
            update.identity = { cell, actor.refNum, actor.mpNum };
            update.state.action
                = static_cast<mwmp::mechanics::ActorAiAction>(actor.aiAction);
            update.state.coordinates = { actor.aiCoordinates.pos[0],
                actor.aiCoordinates.pos[1], actor.aiCoordinates.pos[2] };
            update.state.distance = actor.aiDistance;
            update.state.duration = actor.aiDuration;
            update.state.repeat = actor.aiShouldRepeat;
            if (actor.hasAiTarget)
            {
                mwmp::mechanics::ActorAiTarget target;
                if (actor.aiTarget.isPlayer)
                {
                    target.kind = mwmp::mechanics::ActorAiTargetKind::Player;
                    target.player = actor.aiTarget.guid.value;
                }
                else
                {
                    target.kind = mwmp::mechanics::ActorAiTargetKind::Reference;
                    target.reference = { cell, actor.aiTarget.refNum,
                        actor.aiTarget.mpNum };
                }
                update.state.target = std::move(target);
            }
            result.push_back(std::move(update));
        }
        return result;
    }

    std::vector<mwmp::mechanics::CombatantRelocation> actorRelocations(
        const std::vector<mwmp::mechanics::ActorCellChangeUpdate>& changes)
    {
        std::vector<mwmp::mechanics::CombatantRelocation> result;
        result.reserve(changes.size());
        for (const mwmp::mechanics::ActorCellChangeUpdate& change : changes)
        {
            const std::uint64_t reference
                = (static_cast<std::uint64_t>(change.source.refNum) << 32)
                | static_cast<std::uint64_t>(change.source.mpNum);
            result.push_back({
                { mwmp::mechanics::CombatantKind::Actor, reference,
                    change.source.cell },
                { mwmp::mechanics::CombatantKind::Actor, reference,
                    change.destinationCell },
                change.transform.position,
            });
        }
        return result;
    }

    std::optional<mwmp::mechanics::ActorRosterAction> actorRosterAction(
        unsigned char action)
    {
        switch (action)
        {
            case mwmp::BaseActorList::SET:
                return mwmp::mechanics::ActorRosterAction::Set;
            case mwmp::BaseActorList::ADD:
                return mwmp::mechanics::ActorRosterAction::Add;
            case mwmp::BaseActorList::REMOVE:
                return mwmp::mechanics::ActorRosterAction::Remove;
            default:
                return std::nullopt;
        }
    }

    std::vector<mwmp::mechanics::ActorRosterUpdate> actorRosterUpdates(
        const mwmp::BaseActorList& actorList)
    {
        std::vector<mwmp::mechanics::ActorRosterUpdate> result;
        const std::string cell = actorList.cell.getShortDescription();
        result.reserve(actorList.baseActors.size());
        for (const mwmp::BaseActor& actor : actorList.baseActors)
            result.push_back({ { cell, actor.refNum, actor.mpNum }, actor.refId });
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
    mechanics::EquipmentResult equipmentResult{ mechanics::EquipmentDecision::Applied };
    std::vector<mechanics::InventoryItem> candidate;
    if (action)
    {
        result = mInventoryLedger.previewSnapshot(
            { mechanics::InventoryOwnerKind::Player, player.guid.value }, *action,
            inventoryItems(incoming.inventoryChanges), candidate);
        if (result.applied())
            equipmentResult = mEquipmentLedger.validateInventory(player.guid.value, candidate);
    }
    if (result.applied() && equipmentResult.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected inventory action from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        result.applied() ? mechanics::describe(equipmentResult.decision)
                         : mechanics::describe(result.decision),
        violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid inventory actions");
    return false;
}

bool Networking::validatePlayerItemUse(Player& player, const BasePlayer& incoming)
{
    mechanics::ItemUseIntent intent;
    intent.item = { incoming.usedItem.refId, incoming.usedItem.soul,
        incoming.usedItem.charge, incoming.usedItem.enchantmentCharge,
        incoming.usedItem.count };
    intent.usingItemMagic = incoming.usingItemMagic;
    intent.drawState = incoming.itemUseDrawState;

    const mechanics::ItemUseDecision decision = mechanics::ItemUseValidator{}.validate(
        intent, mInventoryLedger.snapshot(
            { mechanics::InventoryOwnerKind::Player, player.guid.value }));
    if (decision == mechanics::ItemUseDecision::Accepted)
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected item-use intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid item-use intents");
    return false;
}

bool Networking::commitPlayerInventory(Player& player)
{
    const auto action = inventoryAction(player.inventoryChanges.action);
    mechanics::InventoryResult result{ mechanics::InventoryDecision::InvalidAction };
    mechanics::EquipmentResult equipmentResult{ mechanics::EquipmentDecision::Applied };
    std::vector<mechanics::InventoryItem> candidate;
    if (action)
    {
        result = mInventoryLedger.previewSnapshot(
            { mechanics::InventoryOwnerKind::Player, player.guid.value }, *action,
            inventoryItems(player.inventoryChanges), candidate);
        if (result.applied())
            equipmentResult = mEquipmentLedger.validateInventory(player.guid.value, candidate);
        if (result.applied() && equipmentResult.applied())
        {
            result = mInventoryLedger.apply(
                { mechanics::InventoryOwnerKind::Player, player.guid.value }, *action,
                inventoryItems(player.inventoryChanges));
        }
    }
    if (result.applied() && equipmentResult.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified inventory intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        result.applied() ? mechanics::describe(equipmentResult.decision)
                         : mechanics::describe(result.decision),
        violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid inventory actions");
    return false;
}

bool Networking::applyServerInventoryChanges(Player& player)
{
    const auto action = inventoryAction(player.inventoryChanges.action);
    if (!action)
        return false;
    std::vector<mechanics::InventoryItem> candidate;
    mechanics::InventoryResult result = mInventoryLedger.previewSnapshot(
        { mechanics::InventoryOwnerKind::Player, player.guid.value }, *action,
        inventoryItems(player.inventoryChanges), candidate);
    mechanics::EquipmentResult equipmentResult{ mechanics::EquipmentDecision::Applied };
    if (result.applied())
        equipmentResult = mEquipmentLedger.validateInventory(player.guid.value, candidate);
    if (result.applied() && equipmentResult.applied())
    {
        result = mInventoryLedger.apply(
            { mechanics::InventoryOwnerKind::Player, player.guid.value }, *action,
            inventoryItems(player.inventoryChanges));
    }
    if (!result.applied() || !equipmentResult.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored inventory action for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            result.applied() ? mechanics::describe(equipmentResult.decision)
                             : mechanics::describe(result.decision));
    }
    return result.applied() && equipmentResult.applied();
}

bool Networking::validatePlayerSpellbook(
    Player& player, const BasePlayer& incoming)
{
    const auto action = spellbookAction(incoming.spellbookChanges.action);
    mechanics::SpellbookResult result{
        mechanics::SpellbookDecision::InvalidAction };
    if (action)
    {
        result = mSpellbookLedger.preview(player.guid.value, *action,
            spellbookIds(incoming.spellbookChanges));
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mSpellbookViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected spellbook action from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid spellbook actions");
    return false;
}

bool Networking::commitPlayerSpellbook(Player& player)
{
    const auto action = spellbookAction(player.spellbookChanges.action);
    mechanics::SpellbookResult result{
        mechanics::SpellbookDecision::InvalidAction };
    if (action)
    {
        result = mSpellbookLedger.apply(player.guid.value, *action,
            spellbookIds(player.spellbookChanges));
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mSpellbookViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified spellbook intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid spellbook actions");
    return false;
}

bool Networking::applyServerPlayerSpellbook(Player& player)
{
    const auto action = spellbookAction(player.spellbookChanges.action);
    if (!action)
        return false;
    const mechanics::SpellbookResult result = mSpellbookLedger.apply(
        player.guid.value, *action, spellbookIds(player.spellbookChanges));
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored spellbook action for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::validatePlayerEquipment(Player& player, const BasePlayer& incoming)
{
    mechanics::EquipmentResult result{ mechanics::EquipmentDecision::MissingInventory };
    const auto inventory = mInventoryLedger.snapshot(
        { mechanics::InventoryOwnerKind::Player, player.guid.value });
    if (inventory)
    {
        result = mEquipmentLedger.preview(player.guid.value, incoming.exchangeFullInfo,
            equipmentChanges(incoming), *inventory);
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected equipment action from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid equipment actions");
    return false;
}

bool Networking::commitPlayerEquipment(Player& player)
{
    mechanics::EquipmentResult result{ mechanics::EquipmentDecision::MissingInventory };
    const auto inventory = mInventoryLedger.snapshot(
        { mechanics::InventoryOwnerKind::Player, player.guid.value });
    if (inventory)
    {
        result = mEquipmentLedger.apply(player.guid.value, player.exchangeFullInfo,
            equipmentChanges(player), *inventory);
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified equipment intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid equipment actions");
    return false;
}

bool Networking::applyServerPlayerEquipment(Player& player)
{
    const auto inventory = mInventoryLedger.snapshot(
        { mechanics::InventoryOwnerKind::Player, player.guid.value });
    if (!inventory)
        return false;
    return mEquipmentLedger.apply(player.guid.value, player.exchangeFullInfo,
        equipmentChanges(player), *inventory).applied();
}

bool Networking::validateContainerAction(Player& player, const BaseObjectList& incoming)
{
    const ContainerOperations operations = containerOperations(incoming, mInventoryLedger);
    mechanics::InventoryResult result{ operations.decision };
    if (operations.decision == mechanics::InventoryDecision::Applied)
        result = mInventoryLedger.previewBatch(operations.operations);
    if (result.applied())
        return true;

    const unsigned int violations = ++mInventoryViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected container action from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid container actions");
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

    const unsigned int violations = ++mInventoryViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected container intent at commit for connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid container actions");
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
    mwmp::mechanics::ObjectState canonicalPlacedObject(
        const mwmp::BaseObject& object, std::string cell,
        std::uint64_t creator, std::uint32_t mpNum)
    {
        mwmp::mechanics::ObjectState result;
        result.identity = { std::move(cell), object.refNum, mpNum };
        result.refId = object.refId;
        result.soul = object.soul;
        result.creator = creator;
        result.count = object.count;
        result.charge = object.charge;
        result.enchantmentCharge = object.enchantmentCharge;
        result.goldValue = object.goldValue;
        for (std::size_t index = 0; index < 3; ++index)
        {
            result.position[index] = object.position.pos[index];
            result.rotation[index] = object.position.rot[index];
        }
        result.scale = object.scale;
        result.lockLevel = object.lockLevel;
        result.doorState = object.doorState;
        result.enabled = true;
        result.hasContainer = object.hasContainer;
        return result;
    }

    mwmp::mechanics::ObjectState canonicalStaticObject(
        const mwmp::BaseObject& object, std::string cell, std::uint64_t creator)
    {
        mwmp::mechanics::ObjectState result;
        result.identity = { std::move(cell), object.refNum, object.mpNum };
        result.refId = object.refId;
        result.creator = creator;
        result.count = 1;
        result.charge = -1;
        result.enchantmentCharge = -1.0;
        result.enabled = true;
        return result;
    }

    mwmp::mechanics::ObjectState canonicalSpawnedObject(
        const mwmp::BaseObject& object, std::string cell,
        std::uint64_t creator, std::uint32_t mpNum)
    {
        mwmp::mechanics::ObjectState result;
        result.identity = { std::move(cell), object.refNum, mpNum };
        result.refId = object.refId;
        result.creator = creator;
        result.count = 1;
        result.charge = -1;
        result.enchantmentCharge = -1.0;
        for (std::size_t index = 0; index < 3; ++index)
        {
            result.position[index] = object.position.pos[index];
            result.rotation[index] = object.position.rot[index];
        }
        result.enabled = true;
        result.hasContainer = true;
        return result;
    }

    mwmp::mechanics::ObjectMutation canonicalObjectMutation(
        const mwmp::BaseObject& object, std::string cell,
        mwmp::mechanics::ObjectMutationKind kind)
    {
        mwmp::mechanics::ObjectMutation mutation;
        mutation.kind = kind;
        mutation.object.identity = { std::move(cell), object.refNum, object.mpNum };
        mutation.object.enabled = object.objectState;
        mutation.object.scale = object.scale;
        mutation.object.lockLevel = object.lockLevel;
        mutation.object.doorState = object.doorState;
        for (std::size_t index = 0; index < 3; ++index)
        {
            mutation.object.position[index] = object.position.pos[index];
            mutation.object.rotation[index] = object.position.rot[index];
        }
        return mutation;
    }

    bool isClientObjectMutation(mwmp::mechanics::ObjectMutationKind kind) noexcept
    {
        using Kind = mwmp::mechanics::ObjectMutationKind;
        return kind == Kind::SetEnabled || kind == Kind::Move
            || kind == Kind::Rotate || kind == Kind::Scale
            || kind == Kind::SetLock || kind == Kind::SetDoorState
            || kind == Kind::Delete;
    }
}

bool Networking::validateObjectPlace(Player& player, const BaseObjectList& incoming)
{
    mechanics::ObjectResult result{ mechanics::ObjectDecision::InvalidObject };
    const std::string cellDescription = incoming.cell.getShortDescription();
    if (!cellDescription.empty()
        && incoming.packetOrigin <= PACKET_ORIGIN::CLIENT_SCRIPT_GLOBAL
        && !incoming.baseObjects.empty()
        && incoming.baseObjectCount == incoming.baseObjects.size()
        && currentMpNum >= 0
        && incoming.baseObjects.size()
            <= static_cast<std::size_t>(std::numeric_limits<int>::max() - currentMpNum))
    {
        std::vector<mechanics::ObjectMutation> mutations;
        mutations.reserve(incoming.baseObjects.size());
        for (const BaseObject& object : incoming.baseObjects)
        {
            const int assignedMpNum = currentMpNum
                + static_cast<int>(mutations.size()) + 1;
            mutations.push_back({ mechanics::ObjectMutationKind::Place,
                canonicalPlacedObject(object, cellDescription, player.guid.value,
                    static_cast<std::uint32_t>(assignedMpNum)) });
        }
        result = mObjectStateLedger.previewBatch(mutations);
        if (result.applied())
        {
            // Reserve the complete range only after the batch has passed native
            // validation. Skipped IDs are safe if a later Lua policy rejects it.
            for (std::size_t index = 0; index < mutations.size(); ++index)
                incrementMpNum();
            mPendingObjectPlacements.insert_or_assign(
                player.guid.value, std::move(mutations));
            return true;
        }
    }

    const unsigned int violations = ++mObjectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected object placement from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid object placements");
    return false;
}

bool Networking::prepareObjectPlace(Player& player, BaseObjectList& objectList)
{
    const auto pending = mPendingObjectPlacements.find(player.guid.value);
    if (pending == mPendingObjectPlacements.end()
        || pending->second.size() != objectList.baseObjects.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < pending->second.size(); ++index)
        objectList.baseObjects[index].mpNum
            = pending->second[index].object.identity.mpNum;
    return true;
}

bool Networking::commitObjectPlace(Player& player)
{
    const auto pending = mPendingObjectPlacements.find(player.guid.value);
    if (pending == mPendingObjectPlacements.end())
        return false;

    std::vector<mechanics::ObjectMutation> mutations = std::move(pending->second);
    mPendingObjectPlacements.erase(pending);
    const mechanics::ObjectResult result = mObjectStateLedger.applyBatch(mutations);
    if (result.applied())
        return true;

    const unsigned int violations = ++mObjectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected object placement at commit from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid object placements");
    return false;
}

void Networking::cancelObjectPlace(Player& player) noexcept
{
    mPendingObjectPlacements.erase(player.guid.value);
}

bool Networking::seedServerObjectState(const BaseObjectList& objectList)
{
    const std::string cellDescription = objectList.cell.getShortDescription();
    std::vector<mechanics::ObjectMutation> mutations;
    if (cellDescription.empty()
        || objectList.baseObjects.size() > mechanics::ObjectStateLedger::MaximumChanges)
    {
        return false;
    }

    mutations.reserve(objectList.baseObjects.size());
    for (const BaseObject& object : objectList.baseObjects)
    {
        mechanics::ObjectState state = canonicalPlacedObject(
            object, cellDescription, 0, object.mpNum);
        if (mObjectStateLedger.find(state.identity))
            continue;
        mutations.push_back({ mechanics::ObjectMutationKind::Seed, std::move(state) });
    }

    const mechanics::ObjectResult result = mObjectStateLedger.applyBatch(mutations);
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Failed to seed canonical object state for %s: %s",
            cellDescription.c_str(), mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::validateObjectMutation(Player& player,
    const BaseObjectList& incoming, mechanics::ObjectMutationKind kind)
{
    mPendingObjectMutations.erase(player.guid.value);
    mechanics::ObjectResult result{ mechanics::ObjectDecision::InvalidMutation };
    const std::string cellDescription = incoming.cell.getShortDescription();
    if (!isClientObjectMutation(kind))
        return false;

    if (!cellDescription.empty()
        && incoming.packetOrigin <= PACKET_ORIGIN::CLIENT_SCRIPT_GLOBAL
        && !incoming.baseObjects.empty()
        && incoming.baseObjectCount == incoming.baseObjects.size()
        && incoming.baseObjects.size() <= mechanics::ObjectStateLedger::MaximumChanges)
    {
        std::vector<mechanics::ObjectMutation> mutations;
        mutations.reserve(incoming.baseObjects.size() * 2);
        bool complete = true;
        for (const BaseObject& object : incoming.baseObjects)
        {
            mechanics::ObjectMutation mutation = canonicalObjectMutation(
                object, cellDescription, kind);
            if (!mObjectStateLedger.find(mutation.object.identity))
            {
                if (object.refNum == 0 || object.mpNum != 0)
                {
                    result.decision = mechanics::ObjectDecision::MissingObject;
                    complete = false;
                    break;
                }
                mutations.push_back({ mechanics::ObjectMutationKind::Seed,
                    canonicalStaticObject(object, cellDescription, player.guid.value) });
            }
            mutations.push_back(std::move(mutation));
        }

        if (complete)
        {
            result = mObjectStateLedger.previewBatch(mutations);
            if (result.applied())
            {
                mPendingObjectMutations.insert_or_assign(
                    player.guid.value, std::move(mutations));
                return true;
            }
        }
    }

    const unsigned int violations = ++mObjectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected object mutation from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid object mutations");
    return false;
}

bool Networking::validateObjectActivation(Player& player,
    const BaseObjectList& incoming)
{
    mPendingObjectMutations.erase(player.guid.value);
    mechanics::ObjectResult result{ mechanics::ObjectDecision::InvalidObject };
    const std::string cellDescription = incoming.cell.getShortDescription();
    if (!cellDescription.empty()
        && incoming.packetOrigin <= PACKET_ORIGIN::CLIENT_SCRIPT_GLOBAL
        && !incoming.baseObjects.empty()
        && incoming.baseObjectCount == incoming.baseObjects.size()
        && incoming.baseObjects.size() <= mechanics::ObjectStateLedger::MaximumChanges)
    {
        std::vector<mechanics::ObjectMutation> seeds;
        seeds.reserve(incoming.baseObjects.size());
        bool complete = true;
        for (const BaseObject& object : incoming.baseObjects)
        {
            if (!object.activatingActor.isPlayer
                || object.activatingActor.guid.value != player.guid.value)
            {
                complete = false;
                break;
            }
            if (object.isPlayer)
            {
                if (!Players::doesPlayerExist(object.guid))
                    complete = false;
                if (!complete)
                    break;
                continue;
            }

            const mechanics::ObjectIdentity identity{
                cellDescription, object.refNum, object.mpNum };
            const auto existing = mObjectStateLedger.find(identity);
            if (existing)
            {
                if (existing->deleted)
                {
                    result.decision = mechanics::ObjectDecision::DeletedObject;
                    complete = false;
                    break;
                }
                continue;
            }
            if (object.refNum == 0 || object.mpNum != 0)
            {
                result.decision = mechanics::ObjectDecision::MissingObject;
                complete = false;
                break;
            }
            seeds.push_back({ mechanics::ObjectMutationKind::Seed,
                canonicalStaticObject(object, cellDescription, player.guid.value) });
        }

        if (complete)
        {
            result = mObjectStateLedger.previewBatch(seeds);
            if (result.applied())
            {
                mPendingObjectMutations.insert_or_assign(
                    player.guid.value, std::move(seeds));
                return true;
            }
        }
    }

    const unsigned int violations = ++mObjectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected object activation from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid object activations");
    return false;
}

bool Networking::validateObjectSpawn(Player& player, const BaseObjectList& incoming)
{
    mPendingObjectMutations.erase(player.guid.value);
    mechanics::ObjectResult result{ mechanics::ObjectDecision::InvalidObject };
    const std::string cellDescription = incoming.cell.getShortDescription();
    if (!cellDescription.empty()
        && incoming.packetOrigin <= PACKET_ORIGIN::CLIENT_SCRIPT_GLOBAL
        && !incoming.baseObjects.empty()
        && incoming.baseObjectCount == incoming.baseObjects.size()
        && incoming.baseObjects.size() <= mechanics::ObjectStateLedger::MaximumChanges
        && currentMpNum >= 0
        && incoming.baseObjects.size()
            <= static_cast<std::size_t>(std::numeric_limits<int>::max() - currentMpNum))
    {
        std::vector<mechanics::ObjectMutation> mutations;
        mutations.reserve(incoming.baseObjects.size());
        bool complete = true;
        const auto authority = mAuthorityLeases.find(cellDescription);
        const auto now = session::AuthorityLeaseManager::Clock::now();
        for (const BaseObject& object : incoming.baseObjects)
        {
            if (object.isSummon)
            {
                if (object.summonEffectId < 0 || object.summonEffectId > 1'000'000
                    || object.summonSpellId.empty()
                    || !std::isfinite(object.summonDuration)
                    || object.summonDuration <= 0.f
                    || object.summonDuration > 604'800.f)
                {
                    complete = false;
                    break;
                }
                if (object.master.isPlayer)
                {
                    if (object.master.guid.value != player.guid.value)
                    {
                        complete = false;
                        break;
                    }
                }
                else if (!authority || authority->owner != player.guid.value
                    || authority->expiresAt <= now
                    || object.master.refId.empty()
                    || (object.master.refNum == 0 && object.master.mpNum == 0))
                {
                    complete = false;
                    break;
                }
            }

            const int assignedMpNum = currentMpNum
                + static_cast<int>(mutations.size()) + 1;
            mutations.push_back({ mechanics::ObjectMutationKind::Place,
                canonicalSpawnedObject(object, cellDescription, player.guid.value,
                    static_cast<std::uint32_t>(assignedMpNum)) });
        }

        if (complete)
        {
            result = mObjectStateLedger.previewBatch(mutations);
            if (result.applied())
            {
                for (std::size_t index = 0; index < mutations.size(); ++index)
                    incrementMpNum();
                mPendingObjectMutations.insert_or_assign(
                    player.guid.value, std::move(mutations));
                return true;
            }
        }
    }

    const unsigned int violations = ++mObjectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected object spawn from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid object spawns");
    return false;
}

bool Networking::prepareObjectMutationIds(Player& player, BaseObjectList& objectList)
{
    const auto pending = mPendingObjectMutations.find(player.guid.value);
    if (pending == mPendingObjectMutations.end()
        || pending->second.size() != objectList.baseObjects.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < pending->second.size(); ++index)
        objectList.baseObjects[index].mpNum
            = pending->second[index].object.identity.mpNum;
    return true;
}

bool Networking::commitObjectMutation(Player& player)
{
    const auto pending = mPendingObjectMutations.find(player.guid.value);
    if (pending == mPendingObjectMutations.end())
        return false;

    std::vector<mechanics::ObjectMutation> mutations = std::move(pending->second);
    mPendingObjectMutations.erase(pending);
    const mechanics::ObjectResult result = mObjectStateLedger.applyBatch(mutations);
    if (result.applied())
        return true;

    const unsigned int violations = ++mObjectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected object mutation at commit from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid object mutations");
    return false;
}

void Networking::cancelObjectMutation(Player& player) noexcept
{
    mPendingObjectMutations.erase(player.guid.value);
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
                if (spell.caster.guid.value == 0)
                    return false;
                canonical.caster = mwmp::mechanics::CombatantId{
                    mwmp::mechanics::CombatantKind::Player, spell.caster.guid.value, {} };
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

    bool activeEffectAcknowledgement(const mwmp::SpellsActiveChanges& changes,
        std::string_view actorScope,
        const mwmp::mechanics::ActiveEffectLedger& ledger,
        mwmp::mechanics::CombatantId owner)
    {
        if (changes.action != mwmp::SpellsActiveChanges::REMOVE
            || changes.activeSpells.empty())
        {
            return false;
        }
        std::vector<mwmp::mechanics::CanonicalActiveSpell> spells;
        if (!canonicalActiveSpells(changes, actorScope, spells))
            return false;
        return ledger.acknowledgeExpiry(
            std::move(owner), spells).applied();
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
    if (activeEffectAcknowledgement(incoming.spellsActiveChanges,
            player.cell.getShortDescription(), mActiveEffectLedger,
            { mechanics::CombatantKind::Player, player.guid.value, {} }))
        return true;
    const unsigned int violations = ++mActiveEffectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected client-authored active-effect state from connection %llu "
        "(violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value },
            "repeated client-authored active-effect state");
    return false;
}

bool Networking::applyServerPlayerActiveEffects(Player& player)
{
    const auto action = activeEffectAction(player.spellsActiveChanges.action);
    std::vector<mechanics::CanonicalActiveSpell> spells;
    mechanics::ActiveEffectResult result{
        mechanics::ActiveEffectDecision::InvalidAction };
    mechanics::ActiveEffectOperation operation;
    operation.owner = { mechanics::CombatantKind::Player, player.guid.value, {} };
    if (action && canonicalActiveSpells(player.spellsActiveChanges,
            player.cell.getShortDescription(), spells))
    {
        operation.action = *action;
        operation.spells = std::move(spells);
        result = mActiveEffectLedger.apply(
            operation.owner, operation.action, operation.spells);
    }
    else if (action)
        result.decision = mechanics::ActiveEffectDecision::InvalidSpell;
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored active-effect change for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::validateActorActiveEffects(Player& player,
    const BaseActorList& incoming)
{
    Cell* cell = CellController::get()->getCell(&incoming.cell);
    bool valid = cell != nullptr && *cell->getAuthority() == player.guid
        && cell->getAuthorityLeaseId() == incoming.authorityLeaseId
        && incoming.count == incoming.baseActors.size();
    for (const BaseActor& actor : incoming.baseActors)
    {
        valid = valid && cell != nullptr
            && cell->getActor(actor.refNum, actor.mpNum) != nullptr
            && activeEffectAcknowledgement(actor.spellsActiveChanges,
                incoming.cell.getShortDescription(), mActiveEffectLedger,
                { mechanics::CombatantKind::Actor,
                    activeEffectReference(actor.refNum, actor.mpNum),
                    incoming.cell.getShortDescription() });
    }
    if (valid)
        return true;

    const unsigned int violations = ++mActiveEffectViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected client-authored actor active-effect state from connection %llu "
        "(violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value },
            "repeated client-authored actor active-effect state");
    return false;
}

bool Networking::applyServerActorActiveEffects(const BaseActorList& actorList)
{
    const ActiveEffectOperations operations = actorActiveEffectOperations(actorList);
    mechanics::ActiveEffectResult result{ operations.decision };
    if (operations.decision == mechanics::ActiveEffectDecision::Applied)
    {
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


bool Networking::validateActorEquipment(Player& player,
    const BaseActorList& incoming)
{
    mechanics::ActorStateResult result{ mechanics::ActorStateDecision::InvalidBatch };
    ESM::Cell cell = incoming.cell;
    Cell* serverCell = CellController::get()->getCell(&cell);
    bool actorsExist = serverCell != nullptr
        && *serverCell->getAuthority() == player.guid
        && serverCell->getAuthorityLeaseId() == incoming.authorityLeaseId
        && incoming.count == incoming.baseActors.size();
    for (const BaseActor& actor : incoming.baseActors)
    {
        actorsExist = actorsExist
            && serverCell != nullptr
            && serverCell->getActor(actor.refNum, actor.mpNum) != nullptr;
    }
    if (actorsExist)
        result = mActorStateLedger.previewEquipment(actorEquipmentUpdates(incoming));
    if (result.applied())
        return true;

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor equipment from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        actorsExist ? mechanics::describe(result.decision)
                    : "the actor is absent or its authority lease is stale",
        violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor equipment");
    return false;
}

bool Networking::commitActorEquipment(Player& player, BaseActorList& actorList)
{
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    mechanics::ActorStateResult result{ mechanics::ActorStateDecision::InvalidIdentity };
    if (serverCell != nullptr
        && *serverCell->getAuthority() == player.guid
        && serverCell->getAuthorityLeaseId() == actorList.authorityLeaseId)
    {
        result = mActorStateLedger.applyEquipment(actorEquipmentUpdates(actorList));
        if (result.applied())
        {
            serverCell->readActorList(ID_ACTOR_EQUIPMENT, &actorList);
            return true;
        }
    }

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified actor equipment from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor equipment");
    return false;
}

bool Networking::applyServerActorEquipment(BaseActorList& actorList)
{
    actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    bool actorsExist = serverCell != nullptr;
    for (const BaseActor& actor : actorList.baseActors)
    {
        actorsExist = actorsExist
            && serverCell != nullptr
            && serverCell->getActor(actor.refNum, actor.mpNum) != nullptr;
    }
    const mechanics::ActorStateResult result = actorsExist
        ? mActorStateLedger.applyEquipment(actorEquipmentUpdates(actorList))
        : mechanics::ActorStateResult{ mechanics::ActorStateDecision::InvalidIdentity };
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored actor equipment for %s: %s",
            actorList.cell.getShortDescription().c_str(),
            mechanics::describe(result.decision));
        return false;
    }
    serverCell->readActorList(ID_ACTOR_EQUIPMENT, &actorList);
    return true;
}

bool Networking::validActorAiTargets(const BaseActorList& actorList) const
{
    const std::string cell = actorList.cell.getShortDescription();
    for (const BaseActor& actor : actorList.baseActors)
    {
        if (!actor.hasAiTarget)
            continue;

        if (actor.aiTarget.isPlayer)
        {
            const Player* target = Players::getPlayer(actor.aiTarget.guid);
            if (target == nullptr
                || !mAuthenticatedConnections.contains(actor.aiTarget.guid.value)
                || target->cell.getShortDescription() != cell)
            {
                return false;
            }
            continue;
        }

        // Activate may legitimately name a static non-actor reference that the
        // headless server has never materialized. Combat, escort and follow
        // targets, however, must be actors in the canonical cell roster.
        if (actor.aiAction != BaseActorList::ACTIVATE
            && !mActorStateLedger.contains({ cell, actor.aiTarget.refNum,
                actor.aiTarget.mpNum }))
        {
            return false;
        }
    }
    return true;
}

bool Networking::validateActorAi(Player& player, const BaseActorList& incoming)
{
    Cell* serverCell = CellController::get()->getCell(&incoming.cell);
    bool actorsExist = incoming.count == incoming.baseActors.size()
        && serverCell != nullptr
        && *serverCell->getAuthority() == player.guid
        && serverCell->getAuthorityLeaseId() == incoming.authorityLeaseId;
    for (const BaseActor& actor : incoming.baseActors)
    {
        actorsExist = actorsExist && serverCell != nullptr
            && serverCell->containsActor(actor.refNum, actor.mpNum);
    }

    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidIdentity };
    if (actorsExist)
    {
        if (validActorAiTargets(incoming))
            result = mActorStateLedger.previewAi(actorAiUpdates(incoming));
        else
            result.decision = mechanics::ActorStateDecision::InvalidAiTarget;
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor AI intent from connection %llu for %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        incoming.cell.getShortDescription().c_str(),
        actorsExist ? mechanics::describe(result.decision)
                    : "the actor is absent or its authority lease is stale",
        violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor AI intents");
    return false;
}

bool Networking::commitActorAi(Player& player, BaseActorList& actorList)
{
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidIdentity };
    if (actorList.count == actorList.baseActors.size()
        && serverCell != nullptr
        && *serverCell->getAuthority() == player.guid
        && serverCell->getAuthorityLeaseId() == actorList.authorityLeaseId
        && validActorAiTargets(actorList))
    {
        std::vector<mechanics::ActorAiUpdate> updates = actorAiUpdates(actorList);
        // Reserve the pending relay record before changing canonical state.
        // This makes allocation failure fail closed instead of committing a
        // package that the post-commit callback cannot identify.
        mAcceptedActorAiIntents.insert_or_assign(player.guid.value, updates);
        try
        {
            result = mActorStateLedger.applyAi(updates);
        }
        catch (...)
        {
            mAcceptedActorAiIntents.erase(player.guid.value);
            throw;
        }
        if (result.applied())
        {
            serverCell->readActorList(ID_ACTOR_AI, &actorList);
            mRelayedActorAiIntents.erase(player.guid.value);
            return true;
        }
        mAcceptedActorAiIntents.erase(player.guid.value);
    }

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified actor AI intent from connection %llu for %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        actorList.cell.getShortDescription().c_str(),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor AI intents");
    return false;
}

bool Networking::applyServerActorAi(BaseActorList& actorList)
{
    actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidIdentity };
    std::vector<mechanics::ActorAiUpdate> updates = actorAiUpdates(actorList);
    const auto accepted = mAcceptedActorAiIntents.find(actorList.guid.value);
    if (accepted != mAcceptedActorAiIntents.end()
        && accepted->second == updates)
    {
        mRelayedActorAiIntents.insert(actorList.guid.value);
        return true;
    }

    if (serverCell != nullptr && validActorAiTargets(actorList))
        result = mActorStateLedger.applyAi(updates);
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored actor AI for %s: %s",
            actorList.cell.getShortDescription().c_str(),
            mechanics::describe(result.decision));
        return false;
    }
    serverCell->readActorList(ID_ACTOR_AI, &actorList);
    return true;
}

bool Networking::finishActorAiIntent(Player& player) noexcept
{
    mAcceptedActorAiIntents.erase(player.guid.value);
    return mRelayedActorAiIntents.erase(player.guid.value) != 0;
}

bool Networking::validateActorList(Player& player, const BaseActorList& incoming)
{
    const auto action = actorRosterAction(incoming.action);
    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidRosterAction };
    if (action && incoming.count == incoming.baseActors.size())
    {
        result = mActorStateLedger.previewRoster(*action,
            incoming.cell.getShortDescription(), actorRosterUpdates(incoming));
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor roster from connection %llu for %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        incoming.cell.getShortDescription().c_str(),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor roster changes");
    return false;
}

bool Networking::commitActorList(Player& player, BaseActorList& actorList)
{
    const auto action = actorRosterAction(actorList.action);
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidRosterAction };
    if (action && serverCell != nullptr
        && *serverCell->getAuthority() == player.guid
        && serverCell->getAuthorityLeaseId() == actorList.authorityLeaseId)
    {
        const std::vector<mechanics::ActorIdentity> previousActors
            = mActorStateLedger.identities(actorList.cell.getShortDescription());
        result = mActorStateLedger.applyRoster(*action,
            actorList.cell.getShortDescription(), actorRosterUpdates(actorList));
        if (result.applied())
        {
            eraseRemovedActorState(previousActors);
            serverCell->readActorList(ID_ACTOR_LIST, &actorList);
            return true;
        }
    }

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified actor roster from connection %llu for %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        actorList.cell.getShortDescription().c_str(),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor roster changes");
    return false;
}

bool Networking::applyServerActorList(BaseActorList& actorList)
{
    actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
    if (actorList.action == BaseActorList::REQUEST)
        return true;

    const auto action = actorRosterAction(actorList.action);
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidRosterAction };
    if (action && serverCell != nullptr)
    {
        const std::vector<mechanics::ActorIdentity> previousActors
            = mActorStateLedger.identities(actorList.cell.getShortDescription());
        result = mActorStateLedger.applyRoster(*action,
            actorList.cell.getShortDescription(), actorRosterUpdates(actorList));
        if (result.applied())
            eraseRemovedActorState(previousActors);
    }
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored actor roster for %s: %s",
            actorList.cell.getShortDescription().c_str(),
            mechanics::describe(result.decision));
        return false;
    }
    serverCell->readActorList(ID_ACTOR_LIST, &actorList);
    return true;
}

void Networking::eraseRemovedActorState(
    const std::vector<mechanics::ActorIdentity>& previousActors)
{
    for (const mechanics::ActorIdentity& actor : previousActors)
    {
        if (mActorStateLedger.contains(actor))
            continue;
        const mechanics::CombatantId id{
            mechanics::CombatantKind::Actor,
            (static_cast<std::uint64_t>(actor.refNum) << 32)
                | static_cast<std::uint64_t>(actor.mpNum),
            actor.cell
        };
        mCombatResolver.erase(id);
        mSpellResolver.eraseCombatant(id);
        mActiveEffectLedger.erase(id);
        mInventoryLedger.erase({ mechanics::InventoryOwnerKind::Actor,
            id.value, id.scope });
    }
}

bool Networking::validateActorPositions(Player& player,
    const BaseActorList& incoming)
{
    const mechanics::ActorStateResult result = mActorStateLedger.previewPositions(
        actorPositionUpdates(incoming, mCurrentApplicationSequence),
        mMovementMaximumSpeed, mechanics::ActorStateLedger::Clock::now());
    if (result.applied())
        return true;

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor movement from connection %llu for %s: %s; distance %.3f, allowed %.3f (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        incoming.cell.getShortDescription().c_str(), mechanics::describe(result.decision),
        result.distance, result.allowedDistance, violations);
    try
    {
        const std::string cellDescription = incoming.cell.getShortDescription();
        double travelled = result.distance;
        double allowed = result.allowedDistance;
        unsigned int violationCount = violations;
        Script::Call<Script::CallbackIdentity("OnActorMovementViolation")>(
            player.getId(), cellDescription.c_str(), mechanics::describe(result.decision),
            travelled, allowed, violationCount);
    }
    catch (...)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "%s", "OnActorMovementViolation failed; actor movement remains rejected");
    }
    if (violations >= mMovementViolationLimit)
        disconnectTransport({ player.guid.value }, "repeated invalid actor movement samples");
    return false;
}

bool Networking::commitActorPositions(Player& player, BaseActorList& actorList)
{
    Cell* serverCell = CellController::get()->getCell(&actorList.cell);
    mechanics::ActorStateResult result{ mechanics::ActorStateDecision::InvalidIdentity };
    if (serverCell != nullptr
        && *serverCell->getAuthority() == player.guid
        && serverCell->getAuthorityLeaseId() == actorList.authorityLeaseId)
    {
        result = mActorStateLedger.applyPositions(
            actorPositionUpdates(actorList, mCurrentApplicationSequence),
            mMovementMaximumSpeed, mechanics::ActorStateLedger::Clock::now());
        if (result.applied())
        {
            serverCell->readActorList(ID_ACTOR_POSITION, &actorList);
            return true;
        }
    }

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified actor movement from connection %llu for %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        actorList.cell.getShortDescription().c_str(),
        mechanics::describe(result.decision), violations);
    if (violations >= mMovementViolationLimit)
        disconnectTransport({ player.guid.value }, "repeated invalid actor movement samples");
    return false;
}

bool Networking::validateActorCellChanges(Player& player,
    const BaseActorList& incoming)
{
    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidBatch };
    Cell* sourceCell = CellController::get()->getCell(&incoming.cell);
    if (incoming.count == incoming.baseActors.size()
        && sourceCell != nullptr
        && *sourceCell->getAuthority() == player.guid
        && sourceCell->getAuthorityLeaseId() == incoming.authorityLeaseId)
    {
        const auto changes = actorCellChangeUpdates(
            incoming, mCurrentApplicationSequence);
        const auto relocations = actorRelocations(changes);
        result = mActorStateLedger.previewCellChanges(changes);
        if (result.applied()
            && mCombatResolver.previewRelocations(relocations)
            && mActiveEffectLedger.previewRelocations(relocations))
        {
            for (const BaseActor& actor : incoming.baseActors)
            {
                if (!sourceCell->containsActor(actor.refNum, actor.mpNum))
                {
                    result.decision = mechanics::ActorStateDecision::UnknownActor;
                    break;
                }
                if (Cell* destinationCell
                    = CellController::get()->getCell(&actor.cell);
                    destinationCell != nullptr
                    && destinationCell->containsActor(actor.refNum, actor.mpNum))
                {
                    result.decision
                        = mechanics::ActorStateDecision::DestinationOccupied;
                    break;
                }
            }
            if (result.applied())
                return true;
        }
        else if (result.applied())
            result.decision = mechanics::ActorStateDecision::DestinationOccupied;
    }

    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected actor cell change from connection %llu for %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        incoming.cell.getShortDescription().c_str(),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value },
            "repeated invalid actor cell changes");
    return false;
}

bool Networking::commitActorCellChanges(Player& player, BaseActorList& actorList)
{
    mechanics::ActorStateResult result{
        mechanics::ActorStateDecision::InvalidBatch };
    Cell* sourceCell = CellController::get()->getCell(&actorList.cell);
    if (actorList.count != actorList.baseActors.size()
        || sourceCell == nullptr
        || *sourceCell->getAuthority() != player.guid
        || sourceCell->getAuthorityLeaseId() != actorList.authorityLeaseId)
    {
        goto reject;
    }

    try
    {
        const auto changes = actorCellChangeUpdates(
            actorList, mCurrentApplicationSequence);
        const auto relocations = actorRelocations(changes);
        result = mActorStateLedger.previewCellChanges(changes);
        if (!result.applied()
            || !mCombatResolver.previewRelocations(relocations)
            || !mActiveEffectLedger.previewRelocations(relocations))
        {
            if (result.applied())
                result.decision = mechanics::ActorStateDecision::DestinationOccupied;
            goto reject;
        }

        std::unordered_set<std::uint64_t> movedReferences;
        movedReferences.reserve(actorList.baseActors.size());
        for (const BaseActor& actor : actorList.baseActors)
        {
            if (!sourceCell->containsActor(actor.refNum, actor.mpNum))
            {
                result.decision = mechanics::ActorStateDecision::UnknownActor;
                goto reject;
            }
            movedReferences.insert(
                (static_cast<std::uint64_t>(actor.refNum) << 32)
                    | static_cast<std::uint64_t>(actor.mpNum));
        }

        std::vector<BaseActor> sourceActors
            = sourceCell->getActorList()->baseActors;
        std::erase_if(sourceActors,
            [&movedReferences](const BaseActor& actor) {
                const std::uint64_t reference
                    = (static_cast<std::uint64_t>(actor.refNum) << 32)
                    | static_cast<std::uint64_t>(actor.mpNum);
                return movedReferences.contains(reference);
            });
        Cell::PreparedActorRoster preparedSource
            = sourceCell->prepareActorRoster(std::move(sourceActors));

        struct DestinationRoster
        {
            Cell* cell = nullptr;
            std::vector<BaseActor> actors;
            std::optional<Cell::PreparedActorRoster> prepared;
        };
        std::vector<DestinationRoster> destinations;
        for (const BaseActor& actor : actorList.baseActors)
        {
            Cell* destinationCell = CellController::get()->getCell(&actor.cell);
            if (destinationCell == nullptr)
                continue;
            if (destinationCell->containsActor(actor.refNum, actor.mpNum))
            {
                result.decision
                    = mechanics::ActorStateDecision::DestinationOccupied;
                goto reject;
            }

            auto destination = std::find_if(destinations.begin(), destinations.end(),
                [destinationCell](const DestinationRoster& entry) {
                    return entry.cell == destinationCell;
                });
            if (destination == destinations.end())
            {
                destinations.push_back({ destinationCell,
                    destinationCell->getActorList()->baseActors, std::nullopt });
                destination = std::prev(destinations.end());
            }

            BaseActor moved = *sourceCell->getActor(actor.refNum, actor.mpNum);
            moved.cell = actor.cell;
            moved.position = actor.position;
            moved.direction = actor.direction;
            moved.isFollowerCellChange = actor.isFollowerCellChange;
            moved.hasPositionData = true;
            destination->actors.push_back(std::move(moved));
        }
        for (DestinationRoster& destination : destinations)
        {
            destination.prepared
                = destination.cell->prepareActorRoster(std::move(destination.actors));
        }

        mechanics::ActorStateLedger actors = mActorStateLedger;
        mechanics::CombatResolver combat = mCombatResolver;
        mechanics::ActiveEffectLedger activeEffects = mActiveEffectLedger;
        result = actors.applyCellChanges(changes,
            mechanics::ActorStateLedger::Clock::now());
        if (!result.applied() || !combat.applyRelocations(relocations)
            || !activeEffects.applyRelocations(relocations))
        {
            if (result.applied())
                result.decision = mechanics::ActorStateDecision::DestinationOccupied;
            goto reject;
        }

        mActorStateLedger.swap(actors);
        mCombatResolver.swap(combat);
        mActiveEffectLedger.swap(activeEffects);
        sourceCell->commitActorRoster(std::move(preparedSource));
        for (DestinationRoster& destination : destinations)
            destination.cell->commitActorRoster(std::move(*destination.prepared));
        return true;
    }
    catch (const std::exception& exception)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Failed to prepare actor cell change for %s: %s",
            actorList.cell.getShortDescription().c_str(), exception.what());
    }
    catch (...)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Failed to prepare actor cell change for %s: unknown error",
            actorList.cell.getShortDescription().c_str());
    }

reject:
    const unsigned int violations = ++mActorStateViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified actor cell change from connection %llu for %s: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        actorList.cell.getShortDescription().c_str(),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value },
            "repeated invalid actor cell changes");
    return false;
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
            if (target.guid.value == 0)
                return mwmp::mechanics::CastIntentDecision::InvalidTarget;
            result = mwmp::mechanics::CombatantId{
                mwmp::mechanics::CombatantKind::Player, target.guid.value, {} };
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

    double currentStat(const std::map<ESM::RefId, ESM::StatState<float>>& stats,
        const ESM::RefId& id) noexcept
    {
        const auto found = stats.find(id);
        if (found == stats.end() || !std::isfinite(found->second.mCurrent))
            return 0;
        return std::clamp(static_cast<double>(found->second.mCurrent),
            0.0, mwmp::mechanics::SpellResolver::MaximumStatValue);
    }

    double activeEffectMagnitude(
        const std::optional<std::vector<mwmp::mechanics::CanonicalActiveSpell>>& spells,
        std::string_view effectId)
    {
        double result = 0;
        if (!spells)
            return result;
        for (const mwmp::mechanics::CanonicalActiveSpell& spell : *spells)
        {
            for (const mwmp::mechanics::CanonicalEffect& effect : spell.effects)
            {
                if (Misc::StringUtils::lowerCase(effect.effectId) == effectId)
                    result += effect.magnitude;
            }
        }
        return std::clamp(result, 0.0,
            mwmp::mechanics::SpellResolver::MaximumStatValue);
    }

    void applyMagicDefences(mwmp::mechanics::SpellCombatantState& state,
        const std::optional<std::vector<mwmp::mechanics::CanonicalActiveSpell>>& effects)
    {
        const double resistance = activeEffectMagnitude(effects, "resistmagicka");
        const double weakness = activeEffectMagnitude(effects, "weaknesstomagicka");
        state.resistance = std::clamp((resistance - weakness) / 100.0, 0.0, 1.0);
        state.soundMagnitude = activeEffectMagnitude(effects, "sound");
        state.silenced = activeEffectMagnitude(effects, "silence") > 0;
    }

    mwmp::mechanics::SpellCombatantState playerSpellState(
        const Player& player,
        const mwmp::mechanics::CombatantState& combat,
        const std::optional<std::vector<mwmp::mechanics::CanonicalActiveSpell>>& effects,
        double fatigueBase, double fatigueMultiplier,
        const std::optional<mwmp::mechanics::SpellCombatantState>& existing
            = std::nullopt,
        bool replaceResources = false)
    {
        mwmp::mechanics::SpellCombatantState state = existing.value_or(
            mwmp::mechanics::SpellCombatantState{});
        state.health = combat.health;
        state.maximumHealth = combat.maximumHealth;
        state.position = combat.position;
        state.alive = combat.alive;
        if (!existing || replaceResources)
        {
            state.magicka = std::clamp(
                static_cast<double>(player.creatureStats.mDynamic[1].mCurrent),
                0.0, mwmp::mechanics::SpellResolver::MaximumStatValue);
            state.maximumMagicka = dynamicMaximum(
                player.creatureStats.mDynamic[1]);
        }
        state.fatigue = combat.fatigue;
        state.maximumFatigue = combat.maximumFatigue;
        state.willpower = currentStat(
            player.creatureStats.mAttributes, ESM::Attribute::Willpower);
        state.luck = currentStat(
            player.creatureStats.mAttributes, ESM::Attribute::Luck);
        state.enchantSkill = currentStat(
            player.npcStats.mSkills, ESM::Skill::Enchant);
        state.fatigueTerm = std::max(0.0, fatigueBase
            - fatigueMultiplier * (1.0 - combat.fatigueRatio));
        for (int index = 0; index < ESM::MagicSchool::Length; ++index)
        {
            const ESM::RefId skillId = ESM::MagicSchool::indexToSkillRefId(index);
            state.magicSkills.insert_or_assign(
                Misc::StringUtils::lowerCase(skillId.getRefIdString()),
                currentStat(player.npcStats.mSkills, skillId));
        }
        applyMagicDefences(state, effects);
        return state;
    }

    mwmp::mechanics::SpellCombatantState actorTargetSpellState(
        const mwmp::BaseActor& actor,
        const mwmp::mechanics::CombatantState& combat,
        const std::optional<std::vector<mwmp::mechanics::CanonicalActiveSpell>>& effects,
        const std::optional<mwmp::mechanics::SpellCombatantState>& existing
            = std::nullopt,
        bool replaceResources = false)
    {
        mwmp::mechanics::SpellCombatantState state = existing.value_or(
            mwmp::mechanics::SpellCombatantState{});
        state.health = combat.health;
        state.maximumHealth = combat.maximumHealth;
        if (!existing || replaceResources)
        {
            state.magicka = std::clamp(
                static_cast<double>(actor.creatureStats.mDynamic[1].mCurrent),
                0.0, mwmp::mechanics::SpellResolver::MaximumStatValue);
            state.maximumMagicka = dynamicMaximum(
                actor.creatureStats.mDynamic[1]);
        }
        state.fatigue = combat.fatigue;
        state.maximumFatigue = combat.maximumFatigue;
        state.position = combat.position;
        state.alive = combat.alive;
        applyMagicDefences(state, effects);
        return state;
    }

    mwmp::mechanics::SpellCombatantState actorCasterSpellState(
        const mwmp::BaseActor& actor,
        const mwmp::mechanics::ActorMagicTemplate& actorTemplate,
        const mwmp::mechanics::CombatantState& combat,
        const std::optional<std::vector<mwmp::mechanics::CanonicalActiveSpell>>& effects,
        double fatigueBase, double fatigueMultiplier,
        const std::optional<mwmp::mechanics::SpellCombatantState>& existing
            = std::nullopt,
        bool replaceResources = false)
    {
        mwmp::mechanics::SpellCombatantState state = existing.value_or(
            mwmp::mechanics::SpellCombatantState{});
        state.health = combat.health;
        state.maximumHealth = combat.maximumHealth;
        if (!existing || replaceResources)
        {
            state.magicka = std::clamp(
                static_cast<double>(actor.creatureStats.mDynamic[1].mCurrent),
                0.0, actorTemplate.maximumMagicka);
            state.maximumMagicka = actorTemplate.maximumMagicka;
        }
        state.fatigue = combat.fatigue;
        state.maximumFatigue = combat.maximumFatigue;
        state.position = combat.position;
        state.alive = combat.alive;
        state.willpower = actorTemplate.willpower;
        state.luck = actorTemplate.luck;
        state.enchantSkill = actorTemplate.enchantSkill;
        state.magicSkills = actorTemplate.magicSkills;
        state.fatigueTerm = std::max(0.0, fatigueBase
            - fatigueMultiplier * (1.0 - combat.fatigueRatio));
        applyMagicDefences(state, effects);
        return state;
    }

    mwmp::ActiveSpell wireActiveSpell(
        const mwmp::mechanics::CanonicalActiveSpell& canonical)
    {
        mwmp::ActiveSpell result;
        result.id = canonical.id;
        result.isStackingSpell = canonical.stacking;
        result.timestampDay = canonical.timestampDay;
        result.timestampHour = canonical.timestampHour;
        result.params.mDisplayName = canonical.displayName;
        if (canonical.caster)
        {
            if (canonical.caster->kind == mwmp::mechanics::CombatantKind::Player)
            {
                result.caster.isPlayer = true;
                result.caster.guid = mwmp::transport::TransportConnectionId(
                    canonical.caster->value);
            }
            else
            {
                result.caster.refNum = static_cast<unsigned int>(
                    canonical.caster->value >> 32);
                result.caster.mpNum = static_cast<unsigned int>(
                    canonical.caster->value & 0xffffffffU);
            }
        }
        result.params.mEffects.reserve(canonical.effects.size());
        for (const mwmp::mechanics::CanonicalEffect& canonicalEffect : canonical.effects)
        {
            ESM::ActiveEffect effect{};
            effect.mEffectId = ESM::RefId::stringRefId(canonicalEffect.effectId);
            effect.mArg = ESM::RefId::stringRefId(canonicalEffect.argument);
            effect.mMagnitude = static_cast<float>(canonicalEffect.magnitude);
            effect.mMinMagnitude = effect.mMagnitude;
            effect.mMaxMagnitude = effect.mMagnitude;
            effect.mDuration = static_cast<float>(canonicalEffect.duration);
            effect.mTimeLeft = static_cast<float>(canonicalEffect.timeLeft);
            result.params.mEffects.push_back(std::move(effect));
        }
        return result;
    }

    mwmp::Item wireInventoryItem(
        const mwmp::mechanics::InventoryItem& canonical, std::int64_t count)
    {
        mwmp::Item result;
        result.refId = canonical.refId;
        result.count = static_cast<int>(count);
        result.charge = canonical.charge;
        result.enchantmentCharge
            = static_cast<float>(canonical.enchantmentCharge);
        result.soul = canonical.soul;
        return result;
    }
}

bool Networking::validatePlayerCast(Player& player, const BasePlayer& incoming)
{
    mechanics::CastIntent intent;
    mechanics::CastIntentDecision decision = canonicalCastIntent(
        { mechanics::CombatantKind::Player, player.guid.value, {} }, incoming.cast,
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
                mwmp::transport::TransportConnectionId(intent.target->value));
            if (target == nullptr
                || !mAuthenticatedConnections.contains(target->guid.value)
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

    const unsigned int violations = ++mCastViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid cast intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid cast intents");
    return false;
}

void Networking::sanitizePlayerCast(Player& player) noexcept
{
    player.cast.success = false;
    player.cast.isHit = false;
}

bool Networking::resolvePlayerCast(Player& player, std::string& rejectionReason)
{
    rejectionReason.clear();
    mechanics::CastIntent presentationIntent;
    if (canonicalCastIntent(
            { mechanics::CombatantKind::Player, player.guid.value, {} },
            player.cast, player.cell.getShortDescription(), presentationIntent)
        != mechanics::CastIntentDecision::Accepted)
    {
        rejectionReason = "the canonical cast intent is invalid";
        return false;
    }
    const mechanics::CastIntentDecision intentDecision
        = mCastIntentValidator.validate(presentationIntent);
    if (intentDecision != mechanics::CastIntentDecision::Accepted)
    {
        rejectionReason = mechanics::describe(intentDecision);
        return false;
    }

    const std::string sourceId = Misc::StringUtils::lowerCase(
        presentationIntent.sourceId);
    const auto definition = mSpellResolver.findDefinition(sourceId);
    if (!definition)
    {
        rejectionReason = "the spell is not in the canonical content registry";
        return false;
    }
    const bool itemCast = presentationIntent.kind == mechanics::CastKind::Item;
    if (itemCast != (definition->sourceKind == mechanics::SpellSourceKind::Item))
    {
        rejectionReason = "the cast source kind does not match canonical content";
        return false;
    }
    if (!itemCast && !mSpellbookLedger.contains(player.guid.value, sourceId))
    {
        rejectionReason = "the spell is not in the canonical player spellbook";
        return false;
    }

    const mechanics::CombatantId casterId{
        mechanics::CombatantKind::Player, player.guid.value, {} };
    auto casterCombat = mCombatResolver.find(casterId);
    if (!casterCombat)
    {
        rejectionReason = "the caster has no canonical combat state";
        return false;
    }
    casterCombat->position = { player.position.pos[0], player.position.pos[1],
        player.position.pos[2] };

    mechanics::CombatResolver combat = mCombatResolver;
    if (!combat.upsert(casterId, *casterCombat))
    {
        rejectionReason = "the canonical caster combat state is invalid";
        return false;
    }

    mechanics::SpellResolver spells = mSpellResolver;
    if (!spells.upsertCombatant(casterId, playerSpellState(player,
            *casterCombat, mActiveEffectLedger.snapshot(casterId),
            mSpellFatigueBase, mSpellFatigueMultiplier,
            spells.findCombatant(casterId))))
    {
        rejectionReason = "the canonical caster magic state is invalid";
        return false;
    }

    Player* targetPlayer = nullptr;
    Cell* targetCell = nullptr;
    BaseActor* targetActor = nullptr;
    if (presentationIntent.target)
    {
        if (presentationIntent.target->kind == mechanics::CombatantKind::Player)
        {
            targetPlayer = Players::getPlayer(transport::TransportConnectionId(
                presentationIntent.target->value));
            if (targetPlayer == nullptr)
            {
                rejectionReason = "the target player is unavailable";
                return false;
            }
            if (!mAuthenticatedConnections.contains(targetPlayer->guid.value)
                || targetPlayer->cell.getShortDescription()
                    != player.cell.getShortDescription())
            {
                rejectionReason = "the target player is unauthenticated or in another cell";
                return false;
            }
            auto targetCombat = mCombatResolver.find(*presentationIntent.target);
            if (!targetCombat)
            {
                rejectionReason = "the target player has no canonical combat state";
                return false;
            }
            targetCombat->position = { targetPlayer->position.pos[0],
                targetPlayer->position.pos[1], targetPlayer->position.pos[2] };
            if (!combat.upsert(*presentationIntent.target, *targetCombat))
            {
                rejectionReason = "the canonical target combat state is invalid";
                return false;
            }
            if (!spells.upsertCombatant(*presentationIntent.target,
                    playerSpellState(*targetPlayer, *targetCombat,
                        mActiveEffectLedger.snapshot(*presentationIntent.target),
                        mSpellFatigueBase, mSpellFatigueMultiplier,
                        spells.findCombatant(*presentationIntent.target))))
            {
                rejectionReason = "the canonical target magic state is invalid";
                return false;
            }
        }
        else
        {
            targetCell = CellController::get()->getCell(&player.cell);
            if (targetCell != nullptr)
            {
                targetActor = targetCell->getActor(
                    player.cast.target.refNum, player.cast.target.mpNum);
            }
            if (targetActor == nullptr)
            {
                rejectionReason = "the target actor is absent from canonical cell state";
                return false;
            }
            auto targetCombat = mCombatResolver.find(*presentationIntent.target);
            if (!targetCombat && targetActor->hasStatsDynamicData)
            {
                const mechanics::CombatantState initial = actorCombatState(
                    *targetActor, targetActor, std::nullopt, true);
                if (combat.upsert(*presentationIntent.target, initial))
                    targetCombat = initial;
            }
            if (!targetCombat)
            {
                rejectionReason = "the target actor has no canonical combat state";
                return false;
            }
            targetCombat->position = { targetActor->position.pos[0],
                targetActor->position.pos[1], targetActor->position.pos[2] };
            if (!combat.upsert(*presentationIntent.target, *targetCombat))
            {
                rejectionReason = "the canonical target combat state is invalid";
                return false;
            }
            if (!spells.upsertCombatant(*presentationIntent.target,
                    actorTargetSpellState(*targetActor, *targetCombat,
                        mActiveEffectLedger.snapshot(*presentationIntent.target),
                        spells.findCombatant(*presentationIntent.target))))
            {
                rejectionReason = "the canonical target magic state is invalid";
                return false;
            }
        }
    }

    struct ItemMutation
    {
        mechanics::InventoryItem before;
        mechanics::InventoryItem after;
        bool remove = false;
        bool add = false;
        std::optional<std::size_t> equipmentSlot;
    };
    std::optional<ItemMutation> itemMutation;
    std::optional<double> availableItemCharge;
    const mechanics::InventoryOwner inventoryOwner{
        mechanics::InventoryOwnerKind::Player, player.guid.value };
    if (itemCast)
    {
        const auto inventory = mInventoryLedger.snapshot(inventoryOwner);
        if (!inventory)
        {
            rejectionReason = "the caster has no canonical inventory";
            return false;
        }
        const bool consumable = mConsumableMagicItems.contains(sourceId);
        const auto selected = std::ranges::max_element(*inventory, {},
            [sourceId, consumable, &definition](
                const mechanics::InventoryItem& item) {
                if (Misc::StringUtils::lowerCase(item.refId) != sourceId)
                    return -1.0;
                if (consumable)
                    return 0.0;
                return item.enchantmentCharge < 0
                    ? definition->itemMaximumCharge : item.enchantmentCharge;
            });
        if (selected == inventory->end()
            || Misc::StringUtils::lowerCase(selected->refId) != sourceId)
        {
            rejectionReason = "the magic item is not in the canonical inventory";
            return false;
        }
        const double charge = consumable ? 0.0
            : (selected->enchantmentCharge < 0
                ? definition->itemMaximumCharge : selected->enchantmentCharge);
        availableItemCharge = charge;
        itemMutation.emplace();
        itemMutation->before = *selected;
        itemMutation->before.count = 1;
        itemMutation->after = itemMutation->before;
        itemMutation->remove = consumable;

        if (const auto equipment = mEquipmentLedger.snapshot(player.guid.value))
        {
            for (std::size_t slot = 0; slot < equipment->size(); ++slot)
            {
                const mechanics::EquipmentItem& equipped = equipment->at(slot);
                if (Misc::StringUtils::lowerCase(equipped.refId) == sourceId
                    && equipped.charge == selected->charge
                    && equipped.enchantmentCharge == selected->enchantmentCharge)
                {
                    itemMutation->equipmentSlot = slot;
                    break;
                }
            }
        }
    }

    constexpr double randomScale = 1.0 / 4294967296.0;
    const mechanics::SpellResult result = spells.resolve(
        { casterId, presentationIntent.target, sourceId,
            mCurrentApplicationSequence, availableItemCharge },
        static_cast<double>(randombytes_random()) * randomScale,
        static_cast<double>(randombytes_random()) * randomScale);
    if (!result.resolved())
    {
        rejectionReason = mechanics::describe(result.decision);
        return false;
    }

    mechanics::InventoryLedger inventory = mInventoryLedger;
    mechanics::EquipmentLedger equipment = mEquipmentLedger;
    std::optional<mechanics::EquipmentChange> equipmentChange;
    if (itemMutation)
    {
        std::vector<mechanics::InventoryOperation> operations;
        if (itemMutation->remove)
        {
            operations.push_back({ inventoryOwner,
                mechanics::InventoryAction::Remove, { itemMutation->before } });
        }
        else if (result.itemChargeSpent > 0)
        {
            itemMutation->remove = true;
            itemMutation->add = true;
            itemMutation->after.enchantmentCharge = std::max(0.0,
                *availableItemCharge - result.itemChargeSpent);
            operations.push_back({ inventoryOwner,
                mechanics::InventoryAction::Remove, { itemMutation->before } });
            operations.push_back({ inventoryOwner,
                mechanics::InventoryAction::Add, { itemMutation->after } });
        }
        if (!operations.empty() && !inventory.applyBatch(operations).applied())
        {
            rejectionReason = "the canonical magic-item update failed";
            return false;
        }

        const auto candidateInventory = inventory.snapshot(inventoryOwner);
        if (!candidateInventory)
        {
            rejectionReason = "the canonical inventory disappeared during casting";
            return false;
        }
        if (itemMutation->equipmentSlot)
        {
            mechanics::EquipmentItem updated;
            if (itemMutation->add)
            {
                updated.refId = itemMutation->after.refId;
                updated.count = 1;
                updated.charge = itemMutation->after.charge;
                updated.enchantmentCharge
                    = itemMutation->after.enchantmentCharge;
            }
            equipmentChange = mechanics::EquipmentChange{
                *itemMutation->equipmentSlot, std::move(updated) };
            if (!equipment.apply(player.guid.value, false,
                    { *equipmentChange }, *candidateInventory).applied())
            {
                rejectionReason = "the canonical equipped magic-item update failed";
                return false;
            }
        }
        else if (!equipment.validateInventory(
                     player.guid.value, *candidateInventory).applied())
        {
            rejectionReason = "the magic-item update invalidated canonical equipment";
            return false;
        }
    }

    mechanics::ActiveEffectLedger activeEffects = mActiveEffectLedger;
    std::vector<mechanics::ActiveEffectOperation> activeOperations;
    for (const mechanics::SpellApplication& application : result.applications)
    {
        if (application.activeSpell)
        {
            activeOperations.push_back({ application.target,
                mechanics::ActiveEffectAction::Add,
                { *application.activeSpell } });
        }
    }
    if (!activeOperations.empty()
        && !activeEffects.applyBatch(activeOperations).applied())
    {
        rejectionReason = "the canonical active-effect update failed";
        return false;
    }

    for (const mechanics::SpellApplication& application : result.applications)
    {
        auto state = combat.find(application.target);
        if (!state)
        {
            rejectionReason = "a canonical spell target disappeared";
            return false;
        }
        state->health = application.health;
        state->fatigue = application.fatigue;
        state->fatigueRatio = state->maximumFatigue == 0
            ? 1.0 : state->fatigue / state->maximumFatigue;
        state->alive = !application.died;
        if (!combat.upsert(application.target, *state))
        {
            rejectionReason = "a canonical spell target state became invalid";
            return false;
        }
    }

    mSpellResolver.swap(spells);
    mInventoryLedger.swap(inventory);
    mEquipmentLedger.swap(equipment);
    mActiveEffectLedger.swap(activeEffects);
    mCombatResolver.swap(combat);
    player.cast.success = result.applied();

    std::unordered_map<Player*, std::vector<std::uint8_t>> playerStatChanges;
    playerStatChanges[&player].push_back(1);
    const auto casterMagic = mSpellResolver.findCombatant(casterId);
    if (casterMagic)
    {
        player.creatureStats.mDynamic[1].mCurrent
            = static_cast<float>(casterMagic->magicka);
    }
    std::vector<std::pair<Cell*, BaseActor*>> actorStatChanges;
    for (const mechanics::SpellApplication& application : result.applications)
    {
        const auto canonical = mCombatResolver.find(application.target);
        const auto canonicalMagic = mSpellResolver.findCombatant(application.target);
        if (!canonical)
            continue;
        if (application.target.kind == mechanics::CombatantKind::Player)
        {
            Player* affected = Players::getPlayer(
                transport::TransportConnectionId(application.target.value));
            if (affected != nullptr)
            {
                applyCanonicalHealth(*affected, *canonical);
                applyCanonicalFatigue(*affected, *canonical);
                if (canonicalMagic)
                    applyCanonicalMagicka(*affected, *canonicalMagic);
                playerStatChanges[affected].insert(
                    playerStatChanges[affected].end(), { 0, 1, 2 });
            }
        }
        else if (targetActor != nullptr && targetCell != nullptr
            && application.target == *presentationIntent.target)
        {
            applyCanonicalHealth(*targetActor, *canonical);
            applyCanonicalFatigue(*targetActor, *canonical);
            if (canonicalMagic)
                applyCanonicalMagicka(*targetActor, *canonicalMagic);
            targetActor->hasStatsDynamicData = true;
            actorStatChanges.emplace_back(targetCell, targetActor);
        }
    }

    for (auto& [affected, changes] : playerStatChanges)
    {
        std::ranges::sort(changes);
        changes.erase(std::unique(changes.begin(), changes.end()), changes.end());
        affected->exchangeFullInfo = false;
        affected->statsDynamicIndexChanges = std::move(changes);
        PlayerPacket* packet = playerPacketController->GetPacket(
            ID_PLAYER_STATS_DYNAMIC);
        packet->setPlayer(affected);
        packet->Send(affected->guid);
        affected->sendToLoaded(packet);
    }
    for (const auto& [cell, actor] : actorStatChanges)
    {
        BaseActorList list;
        list.cell = player.cell;
        list.authorityLeaseId = cell->getAuthorityLeaseId();
        list.baseActors.push_back(*actor);
        list.count = 1;
        ActorPacket* packet = actorPacketController->GetPacket(
            ID_ACTOR_STATS_DYNAMIC);
        packet->setActorList(&list);
        cell->sendToLoaded(packet, &list);
    }

    if (itemMutation && itemMutation->remove)
    {
        player.inventoryChanges.action = InventoryChanges::REMOVE;
        player.inventoryChanges.items = {
            wireInventoryItem(itemMutation->before, 1) };
        PlayerPacket* packet = playerPacketController->GetPacket(ID_PLAYER_INVENTORY);
        packet->setPlayer(&player);
        packet->Send(player.guid);
        player.sendToLoaded(packet);
        if (itemMutation->add)
        {
            player.inventoryChanges.action = InventoryChanges::ADD;
            player.inventoryChanges.items = {
                wireInventoryItem(itemMutation->after, 1) };
            packet->Send(player.guid);
            player.sendToLoaded(packet);
        }
    }
    if (equipmentChange)
    {
        const std::size_t slot = equipmentChange->slot;
        Item updated;
        updated.refId = equipmentChange->item.refId;
        updated.count = static_cast<int>(equipmentChange->item.count);
        updated.charge = equipmentChange->item.charge;
        updated.enchantmentCharge
            = static_cast<float>(equipmentChange->item.enchantmentCharge);
        player.equipmentItems[slot] = std::move(updated);
        player.exchangeFullInfo = false;
        player.equipmentIndexChanges = { static_cast<int>(slot) };
        PlayerPacket* packet = playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT);
        packet->setPlayer(&player);
        packet->Send(player.guid);
        player.sendToLoaded(packet);
    }

    for (const mechanics::ActiveEffectOperation& operation : activeOperations)
    {
        if (operation.owner.kind == mechanics::CombatantKind::Player)
        {
            Player* affected = Players::getPlayer(
                transport::TransportConnectionId(operation.owner.value));
            if (affected == nullptr)
                continue;
            affected->spellsActiveChanges.action = SpellsActiveChanges::ADD;
            affected->spellsActiveChanges.activeSpells = {
                wireActiveSpell(operation.spells.front()) };
            PlayerPacket* packet = playerPacketController->GetPacket(
                ID_PLAYER_SPELLS_ACTIVE);
            packet->setPlayer(affected);
            packet->Send(affected->guid);
            affected->sendToLoaded(packet);
        }
        else if (targetActor != nullptr && targetCell != nullptr
            && operation.owner == *presentationIntent.target)
        {
            BaseActor activeActor = *targetActor;
            activeActor.spellsActiveChanges.action = SpellsActiveChanges::ADD;
            activeActor.spellsActiveChanges.activeSpells = {
                wireActiveSpell(operation.spells.front()) };
            BaseActorList list;
            list.cell = player.cell;
            list.authorityLeaseId = targetCell->getAuthorityLeaseId();
            list.baseActors.push_back(std::move(activeActor));
            list.count = 1;
            ActorPacket* packet = actorPacketController->GetPacket(
                ID_ACTOR_SPELLS_ACTIVE);
            packet->setActorList(&list);
            targetCell->sendToLoaded(packet, &list);
        }
    }

    for (const mechanics::SpellApplication& application : result.applications)
    {
        if (!application.died)
            continue;
        if (application.target.kind == mechanics::CombatantKind::Player)
        {
            if (Player* victim = Players::getPlayer(
                    transport::TransportConnectionId(application.target.value)))
            {
                Target killer;
                killer.isPlayer = true;
                killer.guid = player.guid;
                publishCanonicalPlayerDeath(*victim, killer);
            }
        }
        else if (targetActor != nullptr && targetCell != nullptr)
        {
            BaseActorList deathList;
            deathList.cell = player.cell;
            deathList.authorityLeaseId = targetCell->getAuthorityLeaseId();
            BaseActor dead = *targetActor;
            dead.killer.isPlayer = true;
            dead.killer.guid = player.guid;
            deathList.baseActors.push_back(std::move(dead));
            deathList.count = 1;
            ActorPacket* packet = actorPacketController->GetPacket(ID_ACTOR_DEATH);
            packet->setActorList(&deathList);
            targetCell->sendToLoaded(packet, &deathList);
            baseActorList = deathList;
            Script::Call<Script::CallbackIdentity("OnActorDeath")>(
                player.getId(), player.cell.getShortDescription().c_str());
        }
    }
    return true;
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
                    mwmp::transport::TransportConnectionId(intent.target->value));
                if (target == nullptr
                    || !mAuthenticatedConnections.contains(target->guid.value)
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

    const unsigned int violations = ++mCastViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid actor cast list from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor cast intents");
    return false;
}

void Networking::sanitizeActorCast(BaseActor& actor) noexcept
{
    actor.cast.success = false;
    actor.cast.isHit = false;
}

bool Networking::resolveActorCast(Player& player, BaseActorList& actorList,
    std::size_t actorIndex, std::optional<BaseActor>& actorDeath,
    std::string& rejectionReason)
{
    actorDeath.reset();
    rejectionReason.clear();
    if (actorIndex >= actorList.baseActors.size())
    {
        rejectionReason = "the actor cast index is invalid";
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
    BaseActor* casterActor = serverCell->getActor(
        submittedActor.refNum, submittedActor.mpNum);
    if (casterActor == nullptr)
    {
        rejectionReason = "the casting actor is absent from canonical cell state";
        return false;
    }
    const std::string casterRefId = Misc::StringUtils::lowerCase(
        casterActor->refId);
    const auto actorTemplate = mActorMagicRegistry.find(casterRefId);
    if (!actorTemplate)
    {
        rejectionReason = "the casting actor has no canonical content template";
        return false;
    }

    const mechanics::CombatantId casterId = actorCombatantId(
        actorList.cell, *casterActor);
    mechanics::CastIntent presentationIntent;
    if (canonicalCastIntent(casterId, submittedActor.cast,
            actorList.cell.getShortDescription(), presentationIntent)
        != mechanics::CastIntentDecision::Accepted)
    {
        rejectionReason = "the canonical actor cast intent is invalid";
        return false;
    }
    const mechanics::CastIntentDecision intentDecision
        = mCastIntentValidator.validate(presentationIntent);
    if (intentDecision != mechanics::CastIntentDecision::Accepted)
    {
        rejectionReason = mechanics::describe(intentDecision);
        return false;
    }

    const std::string sourceId = Misc::StringUtils::lowerCase(
        presentationIntent.sourceId);
    const auto definition = mSpellResolver.findDefinition(sourceId);
    if (!definition)
    {
        rejectionReason = "the spell is not in the canonical content registry";
        return false;
    }
    const bool itemCast = presentationIntent.kind == mechanics::CastKind::Item;
    if (itemCast != (definition->sourceKind == mechanics::SpellSourceKind::Item))
    {
        rejectionReason = "the cast source kind does not match canonical content";
        return false;
    }
    if (!itemCast && !actorTemplate->spells.contains(sourceId))
    {
        rejectionReason = "the spell is not in the canonical actor spellbook";
        return false;
    }
    if (itemCast)
        submittedActor.cast.itemId = sourceId;
    else
        submittedActor.cast.spellId = sourceId;

    auto casterCombat = mCombatResolver.find(casterId);
    if (!casterCombat && casterActor->hasStatsDynamicData)
    {
        const mechanics::CombatantState initial = actorCombatState(
            *casterActor, casterActor, std::nullopt, true);
        if (mCombatResolver.upsert(casterId, initial))
            casterCombat = initial;
    }
    if (!casterCombat)
    {
        rejectionReason = "the casting actor has no canonical combat state";
        return false;
    }
    casterCombat->maximumHealth = actorTemplate->maximumHealth;
    casterCombat->health = std::clamp(casterCombat->health,
        0.0, casterCombat->maximumHealth);
    casterCombat->alive = casterCombat->health > 0;
    casterCombat->position = { casterActor->position.pos[0],
        casterActor->position.pos[1], casterActor->position.pos[2] };

    mechanics::CombatResolver combat = mCombatResolver;
    if (!combat.upsert(casterId, *casterCombat))
    {
        rejectionReason = "the canonical actor combat state is invalid";
        return false;
    }
    mechanics::SpellResolver spells = mSpellResolver;
    if (!spells.upsertCombatant(casterId, actorCasterSpellState(*casterActor,
            *actorTemplate, *casterCombat, mActiveEffectLedger.snapshot(casterId),
            mSpellFatigueBase, mSpellFatigueMultiplier,
            spells.findCombatant(casterId))))
    {
        rejectionReason = "the canonical actor magic state is invalid";
        return false;
    }

    Player* targetPlayer = nullptr;
    BaseActor* targetActor = nullptr;
    if (presentationIntent.target)
    {
        if (presentationIntent.target->kind == mechanics::CombatantKind::Player)
        {
            targetPlayer = Players::getPlayer(transport::TransportConnectionId(
                presentationIntent.target->value));
            if (targetPlayer == nullptr
                || !mAuthenticatedConnections.contains(targetPlayer->guid.value)
                || targetPlayer->cell.getShortDescription()
                    != actorList.cell.getShortDescription())
            {
                rejectionReason = "the target player is unavailable or in another cell";
                return false;
            }
            auto targetCombat = mCombatResolver.find(*presentationIntent.target);
            if (!targetCombat)
            {
                rejectionReason = "the target player has no canonical combat state";
                return false;
            }
            targetCombat->position = { targetPlayer->position.pos[0],
                targetPlayer->position.pos[1], targetPlayer->position.pos[2] };
            if (!combat.upsert(*presentationIntent.target, *targetCombat)
                || !spells.upsertCombatant(*presentationIntent.target,
                    playerSpellState(*targetPlayer, *targetCombat,
                        mActiveEffectLedger.snapshot(*presentationIntent.target),
                        mSpellFatigueBase, mSpellFatigueMultiplier,
                        spells.findCombatant(*presentationIntent.target))))
            {
                rejectionReason = "the canonical target player state is invalid";
                return false;
            }
        }
        else
        {
            targetActor = serverCell->getActor(submittedActor.cast.target.refNum,
                submittedActor.cast.target.mpNum);
            if (targetActor == nullptr)
            {
                rejectionReason = "the target actor is absent from canonical cell state";
                return false;
            }
            auto targetCombat = mCombatResolver.find(*presentationIntent.target);
            if (!targetCombat && targetActor->hasStatsDynamicData)
            {
                const mechanics::CombatantState initial = actorCombatState(
                    *targetActor, targetActor, std::nullopt, true);
                if (combat.upsert(*presentationIntent.target, initial))
                    targetCombat = initial;
            }
            if (!targetCombat)
            {
                rejectionReason = "the target actor has no canonical combat state";
                return false;
            }
            const auto targetTemplate = mActorMagicRegistry.find(
                Misc::StringUtils::lowerCase(targetActor->refId));
            if (!targetTemplate)
            {
                rejectionReason = "the target actor has no canonical content template";
                return false;
            }
            submittedActor.cast.target.refId = targetActor->refId;
            targetCombat->maximumHealth = targetTemplate->maximumHealth;
            targetCombat->health = std::clamp(targetCombat->health,
                0.0, targetCombat->maximumHealth);
            targetCombat->alive = targetCombat->health > 0;
            targetCombat->position = { targetActor->position.pos[0],
                targetActor->position.pos[1], targetActor->position.pos[2] };
            if (!combat.upsert(*presentationIntent.target, *targetCombat)
                || !spells.upsertCombatant(*presentationIntent.target,
                    actorTargetSpellState(*targetActor, *targetCombat,
                        mActiveEffectLedger.snapshot(*presentationIntent.target),
                        spells.findCombatant(*presentationIntent.target))))
            {
                rejectionReason = "the canonical target actor state is invalid";
                return false;
            }
        }
    }

    struct ItemMutation
    {
        mechanics::InventoryItem before;
        mechanics::InventoryItem after;
        bool remove = false;
        bool add = false;
        std::optional<std::size_t> equipmentSlot;
    };
    std::optional<ItemMutation> itemMutation;
    std::optional<double> availableItemCharge;
    const mechanics::InventoryOwner inventoryOwner{
        mechanics::InventoryOwnerKind::Actor, casterId.value, casterId.scope };
    mechanics::InventoryLedger inventory = mInventoryLedger;
    if (itemCast)
    {
        auto canonicalInventory = inventory.snapshot(inventoryOwner);
        if (!canonicalInventory)
        {
            const mechanics::InventoryResult seeded = inventory.apply(inventoryOwner,
                mechanics::InventoryAction::Set, actorTemplate->inventory);
            if (!seeded.applied())
            {
                rejectionReason = "the canonical actor inventory could not be initialized";
                return false;
            }
            canonicalInventory = inventory.snapshot(inventoryOwner);
        }
        if (!canonicalInventory)
        {
            rejectionReason = "the casting actor has no canonical inventory";
            return false;
        }
        const bool consumable = mConsumableMagicItems.contains(sourceId);
        const auto selected = std::ranges::max_element(*canonicalInventory, {},
            [sourceId, consumable, &definition](
                const mechanics::InventoryItem& item) {
                if (Misc::StringUtils::lowerCase(item.refId) != sourceId)
                    return -1.0;
                if (consumable)
                    return 0.0;
                return item.enchantmentCharge < 0
                    ? definition->itemMaximumCharge : item.enchantmentCharge;
            });
        if (selected == canonicalInventory->end()
            || Misc::StringUtils::lowerCase(selected->refId) != sourceId)
        {
            rejectionReason = "the magic item is not in the canonical actor inventory";
            return false;
        }
        availableItemCharge = consumable ? 0.0
            : (selected->enchantmentCharge < 0
                ? definition->itemMaximumCharge : selected->enchantmentCharge);
        itemMutation.emplace();
        itemMutation->before = *selected;
        itemMutation->before.count = 1;
        itemMutation->after = itemMutation->before;
        itemMutation->remove = consumable;
        for (std::size_t slot = 0; slot < std::size(casterActor->equipmentItems);
            ++slot)
        {
            const Item& equipped = casterActor->equipmentItems[slot];
            if (Misc::StringUtils::lowerCase(equipped.refId) == sourceId
                && equipped.charge == selected->charge
                && equipped.enchantmentCharge == selected->enchantmentCharge)
            {
                itemMutation->equipmentSlot = slot;
                break;
            }
        }
    }

    constexpr double randomScale = 1.0 / 4294967296.0;
    const mechanics::SpellResult result = spells.resolve(
        { casterId, presentationIntent.target, sourceId,
            mCurrentApplicationSequence, availableItemCharge },
        static_cast<double>(randombytes_random()) * randomScale,
        static_cast<double>(randombytes_random()) * randomScale);
    if (!result.resolved())
    {
        rejectionReason = mechanics::describe(result.decision);
        return false;
    }

    if (itemMutation)
    {
        std::vector<mechanics::InventoryOperation> operations;
        if (itemMutation->remove)
        {
            operations.push_back({ inventoryOwner,
                mechanics::InventoryAction::Remove, { itemMutation->before } });
        }
        else if (result.itemChargeSpent > 0)
        {
            itemMutation->remove = true;
            itemMutation->add = true;
            itemMutation->after.enchantmentCharge = std::max(0.0,
                *availableItemCharge - result.itemChargeSpent);
            operations.push_back({ inventoryOwner,
                mechanics::InventoryAction::Remove, { itemMutation->before } });
            operations.push_back({ inventoryOwner,
                mechanics::InventoryAction::Add, { itemMutation->after } });
        }
        if (!operations.empty() && !inventory.applyBatch(operations).applied())
        {
            rejectionReason = "the canonical actor magic-item update failed";
            return false;
        }
    }

    mechanics::ActiveEffectLedger activeEffects = mActiveEffectLedger;
    std::vector<mechanics::ActiveEffectOperation> activeOperations;
    for (const mechanics::SpellApplication& application : result.applications)
    {
        if (application.activeSpell)
        {
            activeOperations.push_back({ application.target,
                mechanics::ActiveEffectAction::Add,
                { *application.activeSpell } });
        }
    }
    if (!activeOperations.empty()
        && !activeEffects.applyBatch(activeOperations).applied())
    {
        rejectionReason = "the canonical actor active-effect update failed";
        return false;
    }
    for (const mechanics::SpellApplication& application : result.applications)
    {
        auto state = combat.find(application.target);
        if (!state)
        {
            rejectionReason = "a canonical actor spell target disappeared";
            return false;
        }
        state->health = application.health;
        state->fatigue = application.fatigue;
        state->fatigueRatio = state->maximumFatigue == 0
            ? 1.0 : state->fatigue / state->maximumFatigue;
        state->alive = !application.died;
        if (!combat.upsert(application.target, *state))
        {
            rejectionReason = "a canonical actor spell target became invalid";
            return false;
        }
    }

    mSpellResolver.swap(spells);
    mInventoryLedger.swap(inventory);
    mActiveEffectLedger.swap(activeEffects);
    mCombatResolver.swap(combat);
    submittedActor.cast.success = result.applied();

    if (const auto casterMagic = mSpellResolver.findCombatant(casterId))
    {
        casterActor->creatureStats.mDynamic[1].mCurrent
            = static_cast<float>(casterMagic->magicka);
        casterActor->hasStatsDynamicData = true;
        submittedActor.creatureStats = casterActor->creatureStats;
        submittedActor.hasStatsDynamicData = true;
    }
    if (itemMutation && itemMutation->equipmentSlot)
    {
        const std::size_t slot = *itemMutation->equipmentSlot;
        Item updated;
        if (itemMutation->add)
            updated = wireInventoryItem(itemMutation->after, 1);
        casterActor->equipmentItems[slot] = updated;
        submittedActor.equipmentItems[slot] = updated;

        BaseActorList equipmentList;
        equipmentList.guid = player.guid;
        equipmentList.cell = actorList.cell;
        equipmentList.authorityLeaseId = serverCell->getAuthorityLeaseId();
        equipmentList.baseActors.push_back(*casterActor);
        equipmentList.count = 1;
        ActorPacket* equipmentPacket = actorPacketController->GetPacket(
            ID_ACTOR_EQUIPMENT);
        equipmentPacket->setActorList(&equipmentList);
        equipmentPacket->Send(player.guid);
        serverCell->sendToLoaded(equipmentPacket, &equipmentList);
    }

    std::vector<BaseActor*> changedActors{ casterActor };
    for (const mechanics::SpellApplication& application : result.applications)
    {
        const auto canonical = mCombatResolver.find(application.target);
        const auto canonicalMagic = mSpellResolver.findCombatant(application.target);
        if (!canonical)
            continue;
        if (application.target.kind == mechanics::CombatantKind::Player)
        {
            Player* affected = Players::getPlayer(
                transport::TransportConnectionId(application.target.value));
            if (affected == nullptr)
                continue;
            applyCanonicalHealth(*affected, *canonical);
            applyCanonicalFatigue(*affected, *canonical);
            if (canonicalMagic)
                applyCanonicalMagicka(*affected, *canonicalMagic);
            affected->exchangeFullInfo = false;
            affected->statsDynamicIndexChanges = { 0, 1, 2 };
            PlayerPacket* statsPacket = playerPacketController->GetPacket(
                ID_PLAYER_STATS_DYNAMIC);
            statsPacket->setPlayer(affected);
            statsPacket->Send(affected->guid);
            affected->sendToLoaded(statsPacket);
        }
        else
        {
            BaseActor* affected = application.target == casterId
                ? casterActor : targetActor;
            if (affected != nullptr)
            {
                applyCanonicalHealth(*affected, *canonical);
                applyCanonicalFatigue(*affected, *canonical);
                if (canonicalMagic)
                    applyCanonicalMagicka(*affected, *canonicalMagic);
                affected->hasStatsDynamicData = true;
                if (std::ranges::find(changedActors, affected)
                    == changedActors.end())
                {
                    changedActors.push_back(affected);
                }
            }
        }
    }
    for (BaseActor* changed : changedActors)
    {
        BaseActorList statsList;
        statsList.guid = player.guid;
        statsList.cell = actorList.cell;
        statsList.authorityLeaseId = serverCell->getAuthorityLeaseId();
        statsList.baseActors.push_back(*changed);
        statsList.count = 1;
        ActorPacket* statsPacket = actorPacketController->GetPacket(
            ID_ACTOR_STATS_DYNAMIC);
        statsPacket->setActorList(&statsList);
        statsPacket->Send(player.guid);
        serverCell->sendToLoaded(statsPacket, &statsList);
    }

    for (const mechanics::ActiveEffectOperation& operation : activeOperations)
    {
        if (operation.owner.kind == mechanics::CombatantKind::Player)
        {
            Player* affected = Players::getPlayer(
                transport::TransportConnectionId(operation.owner.value));
            if (affected == nullptr)
                continue;
            affected->spellsActiveChanges.action = SpellsActiveChanges::ADD;
            affected->spellsActiveChanges.activeSpells = {
                wireActiveSpell(operation.spells.front()) };
            PlayerPacket* packet = playerPacketController->GetPacket(
                ID_PLAYER_SPELLS_ACTIVE);
            packet->setPlayer(affected);
            packet->Send(affected->guid);
            affected->sendToLoaded(packet);
        }
        else
        {
            BaseActor* affected = operation.owner == casterId
                ? casterActor : targetActor;
            if (affected == nullptr)
                continue;
            BaseActor activeActor = *affected;
            activeActor.spellsActiveChanges.action = SpellsActiveChanges::ADD;
            activeActor.spellsActiveChanges.activeSpells = {
                wireActiveSpell(operation.spells.front()) };
            BaseActorList activeList;
            activeList.guid = player.guid;
            activeList.cell = actorList.cell;
            activeList.authorityLeaseId = serverCell->getAuthorityLeaseId();
            activeList.baseActors.push_back(std::move(activeActor));
            activeList.count = 1;
            ActorPacket* packet = actorPacketController->GetPacket(
                ID_ACTOR_SPELLS_ACTIVE);
            packet->setActorList(&activeList);
            serverCell->sendToLoaded(packet, &activeList);
        }
    }

    for (const mechanics::SpellApplication& application : result.applications)
    {
        if (!application.died)
            continue;
        if (application.target.kind == mechanics::CombatantKind::Player)
        {
            if (Player* victim = Players::getPlayer(
                    transport::TransportConnectionId(application.target.value)))
            {
                Target killer;
                killer.refId = casterActor->refId;
                killer.refNum = casterActor->refNum;
                killer.mpNum = casterActor->mpNum;
                killer.name = casterActor->refId;
                publishCanonicalPlayerDeath(*victim, killer);
            }
        }
        else
        {
            BaseActor* victim = application.target == casterId
                ? casterActor : targetActor;
            if (victim != nullptr)
                actorDeath = *victim;
        }
    }
    return true;
}

bool Networking::validatePlayerBounty(Player& player, const BasePlayer& incoming)
{
    const mechanics::JusticeResult result = mJusticeLedger.previewBountyIntent(
        player.guid.value, incoming.npcStats.mBounty);
    if (result.applied())
    {
        mPendingPlayerBounties.insert_or_assign(
            player.guid.value, incoming.npcStats.mBounty);
        return true;
    }

    const unsigned int violations = ++mJusticeViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected bounty intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid bounty intents");
    return false;
}

bool Networking::commitPlayerBounty(Player& player)
{
    const auto pending = mPendingPlayerBounties.find(player.guid.value);
    if (pending == mPendingPlayerBounties.end())
        return false;

    const std::int64_t proposedBounty = player.npcStats.mBounty;
    const bool serverOverride = proposedBounty != pending->second;
    mPendingPlayerBounties.erase(pending);
    const mechanics::JusticeResult result = serverOverride
        ? mJusticeLedger.setBounty(player.guid.value, proposedBounty)
        : mJusticeLedger.applyBountyIntent(player.guid.value, proposedBounty);
    if (result.applied())
    {
        player.npcStats.mBounty = static_cast<std::int32_t>(result.state.bounty);
        return true;
    }

    if (const auto canonical = mJusticeLedger.find(player.guid.value))
        player.npcStats.mBounty = static_cast<std::int32_t>(canonical->bounty);
    const unsigned int violations = ++mJusticeViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified bounty intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid bounty intents");
    return false;
}

bool Networking::applyServerPlayerBounty(Player& player)
{
    const std::int64_t bounty = player.npcStats.mBounty;
    if (mPendingPlayerBounties.contains(player.guid.value))
        return bounty >= 0 && bounty <= mechanics::JusticeLedger::MaximumBounty;

    const mechanics::JusticeResult result = mJusticeLedger.setBounty(
        player.guid.value, bounty);
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored bounty for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::isPlayerBountyIntentPending(const Player& player) const noexcept
{
    return mPendingPlayerBounties.contains(player.guid.value);
}

void Networking::cancelPlayerBountyIntent(Player& player) noexcept
{
    mPendingPlayerBounties.erase(player.guid.value);
    if (const auto canonical = mJusticeLedger.find(player.guid.value))
        player.npcStats.mBounty = static_cast<std::int32_t>(canonical->bounty);
}

bool Networking::beginPlayerJail(Player& player, std::uint32_t days,
    bool ignoreTeleportation, bool ignoreSkillIncreases,
    std::string progressText, std::string endText)
{
    if (!mJusticeLedger.find(player.guid.value))
    {
        const mechanics::JusticeResult seeded = mJusticeLedger.setBounty(
            player.guid.value, player.npcStats.mBounty);
        if (!seeded.applied())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                "Failed to seed justice state for connection %llu: %s",
                static_cast<unsigned long long>(player.guid.value),
                mechanics::describe(seeded.decision));
            return false;
        }
    }

    const mechanics::JusticeResult result = mJusticeLedger.beginSentence(
        player.guid.value, days, ignoreTeleportation, ignoreSkillIncreases,
        std::move(progressText), std::move(endText));
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored jail sentence for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
        return false;
    }

    const mechanics::JailSentence& sentence = *result.state.sentence;
    try
    {
        player.jailAction = JailAction::Begin;
        player.jailSentenceId = sentence.id;
        player.jailDays = static_cast<int>(sentence.days);
        player.ignoreJailTeleportation = sentence.ignoreTeleportation;
        player.ignoreJailSkillIncreases = sentence.ignoreSkillIncreases;
        player.jailProgressText = sentence.progressText;
        player.jailEndText = sentence.endText;
    }
    catch (...)
    {
        mJusticeLedger.completeSentence(player.guid.value, sentence.id, false);
        throw;
    }
    return true;
}

bool Networking::validatePlayerJailCompletion(
    Player& player, const BasePlayer& incoming)
{
    mechanics::JusticeResult result;
    if (incoming.jailAction != JailAction::Complete)
        result.decision = mechanics::JusticeDecision::InvalidSentence;
    else
    {
        result = mJusticeLedger.previewSentenceCompletion(
            player.guid.value, incoming.jailSentenceId);
    }
    if (result.applied())
        return true;

    const unsigned int violations = ++mJusticeViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected jail completion from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid jail completions");
    return false;
}

bool Networking::completePlayerJail(Player& player)
{
    const mechanics::JusticeResult result = mJusticeLedger.completeSentence(
        player.guid.value, player.jailSentenceId, false);
    if (!result.applied())
        return false;

    player.npcStats.mBounty = static_cast<std::int32_t>(result.state.bounty);
    player.jailAction = JailAction::Complete;
    player.jailDays = 0;
    player.ignoreJailTeleportation = false;
    player.ignoreJailSkillIncreases = false;
    player.jailProgressText.clear();
    player.jailEndText.clear();
    return true;
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

    mwmp::mechanics::CombatantState playerCombatState(const Player& player,
        const std::optional<mwmp::mechanics::CombatantState>& existing,
        bool replaceResources)
    {
        mwmp::mechanics::CombatantState state = existing.value_or(
            mwmp::mechanics::CombatantState{});
        const auto& health = player.creatureStats.mDynamic[0];
        if (!existing || replaceResources)
        {
            state.health = std::clamp(static_cast<double>(health.mCurrent),
                0.0, maximumCanonicalStat);
            state.maximumHealth = dynamicMaximum(health);
            state.alive = state.health > 0;
        }
        else
            state.maximumHealth = std::max(state.maximumHealth, state.health);
        if (!existing || replaceResources)
        {
            state.maximumFatigue = dynamicMaximum(
                player.creatureStats.mDynamic[2]);
            state.fatigue = std::clamp(
                static_cast<double>(player.creatureStats.mDynamic[2].mCurrent),
                0.0, state.maximumFatigue);
            state.fatigueRatio = state.maximumFatigue == 0
                ? 1.0 : state.fatigue / state.maximumFatigue;
        }
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

    void applyCanonicalMagicka(Player& player,
        const mwmp::mechanics::SpellCombatantState& state) noexcept
    {
        auto& magicka = player.creatureStats.mDynamic[1];
        magicka.mBase = static_cast<float>(state.maximumMagicka);
        magicka.mMod = static_cast<float>(state.maximumMagicka);
        magicka.mCurrent = static_cast<float>(state.magicka);
        magicka.mDamage = 0;
        magicka.mProgress = 0;
    }

    void applyCanonicalFatigue(Player& player,
        const mwmp::mechanics::CombatantState& state) noexcept
    {
        auto& fatigue = player.creatureStats.mDynamic[2];
        fatigue.mBase = static_cast<float>(state.maximumFatigue);
        fatigue.mMod = static_cast<float>(state.maximumFatigue);
        fatigue.mCurrent = static_cast<float>(state.fatigue);
        fatigue.mDamage = 0;
        fatigue.mProgress = 0;
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
        bool replaceResources)
    {
        mwmp::mechanics::CombatantState state = existing.value_or(
            mwmp::mechanics::CombatantState{});
        const auto& health = actor.creatureStats.mDynamic[0];
        if (!existing || replaceResources)
        {
            state.health = std::clamp(static_cast<double>(health.mCurrent),
                0.0, maximumCanonicalStat);
            state.maximumHealth = dynamicMaximum(health);
            state.alive = state.health > 0;
        }
        else
            state.maximumHealth = std::max(state.maximumHealth, state.health);
        if (!existing || replaceResources)
        {
            state.maximumFatigue = dynamicMaximum(
                actor.creatureStats.mDynamic[2]);
            state.fatigue = std::clamp(
                static_cast<double>(actor.creatureStats.mDynamic[2].mCurrent),
                0.0, state.maximumFatigue);
            state.fatigueRatio = state.maximumFatigue == 0
                ? 1.0 : state.fatigue / state.maximumFatigue;
        }
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

    void applyCanonicalMagicka(mwmp::BaseActor& actor,
        const mwmp::mechanics::SpellCombatantState& state) noexcept
    {
        auto& magicka = actor.creatureStats.mDynamic[1];
        magicka.mBase = static_cast<float>(state.maximumMagicka);
        magicka.mMod = static_cast<float>(state.maximumMagicka);
        magicka.mCurrent = static_cast<float>(state.magicka);
        magicka.mDamage = 0;
        magicka.mProgress = 0;
    }

    void applyCanonicalFatigue(mwmp::BaseActor& actor,
        const mwmp::mechanics::CombatantState& state) noexcept
    {
        auto& fatigue = actor.creatureStats.mDynamic[2];
        fatigue.mBase = static_cast<float>(state.maximumFatigue);
        fatigue.mMod = static_cast<float>(state.maximumFatigue);
        fatigue.mCurrent = static_cast<float>(state.fatigue);
        fatigue.mDamage = 0;
        fatigue.mProgress = 0;
    }
}

bool Networking::validatePlayerShapeshift(
    Player& player, const BasePlayer& incoming)
{
    if (!mShapeshiftLedger.find(player.guid.value))
    {
        const mechanics::ShapeshiftResult seeded = mShapeshiftLedger.set(
            player.guid.value, { player.scale, player.isWerewolf,
                player.displayCreatureName, player.creatureRefId });
        if (!seeded.applied())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                "Failed to seed shapeshift state for connection %llu: %s",
                static_cast<unsigned long long>(player.guid.value),
                mechanics::describe(seeded.decision));
            return false;
        }
    }

    const mechanics::ShapeshiftResult result
        = mShapeshiftLedger.previewClientIntent(player.guid.value,
            { incoming.scale, incoming.isWerewolf,
                incoming.displayCreatureName, incoming.creatureRefId });
    if (result.applied())
    {
        mPendingPlayerShapeshifts.insert(player.guid.value);
        return true;
    }

    const unsigned int violations = ++mShapeshiftViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected shapeshift intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid shapeshift intents");
    return false;
}

bool Networking::commitPlayerShapeshift(Player& player)
{
    if (mPendingPlayerShapeshifts.erase(player.guid.value) == 0)
        return false;

    const mechanics::ShapeshiftResult result
        = mShapeshiftLedger.applyClientIntent(player.guid.value,
            { player.scale, player.isWerewolf,
                player.displayCreatureName, player.creatureRefId });
    if (result.applied())
        return true;

    cancelPlayerShapeshiftIntent(player);
    const unsigned int violations = ++mShapeshiftViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified shapeshift intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid shapeshift intents");
    return false;
}

bool Networking::applyServerPlayerShapeshift(Player& player)
{
    if (mPendingPlayerShapeshifts.contains(player.guid.value))
        return false;
    const mechanics::ShapeshiftResult result = mShapeshiftLedger.set(
        player.guid.value, { player.scale, player.isWerewolf,
            player.displayCreatureName, player.creatureRefId });
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored shapeshift state for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::isPlayerShapeshiftIntentPending(
    const Player& player) const noexcept
{
    return mPendingPlayerShapeshifts.contains(player.guid.value);
}

void Networking::cancelPlayerShapeshiftIntent(Player& player) noexcept
{
    mPendingPlayerShapeshifts.erase(player.guid.value);
    if (const auto canonical = mShapeshiftLedger.find(player.guid.value))
    {
        player.scale = canonical->scale;
        player.isWerewolf = canonical->isWerewolf;
        player.displayCreatureName = canonical->displayCreatureName;
        player.creatureRefId = canonical->creatureRefId;
    }
}

namespace
{
    mwmp::mechanics::ProgressionStat progressionStat(
        const ESM::StatState<float>& state) noexcept
    {
        return { state.mBase, state.mMod, state.mCurrent,
            state.mDamage, state.mProgress };
    }

    ESM::StatState<float> esmProgressionStat(
        const mwmp::mechanics::ProgressionStat& state) noexcept
    {
        ESM::StatState<float> result;
        result.mBase = state.base;
        result.mMod = state.modifier;
        result.mCurrent = state.current;
        result.mDamage = state.damage;
        result.mProgress = state.progress;
        return result;
    }

    mwmp::mechanics::PlayerProgressionState progressionState(
        const mwmp::BasePlayer& player)
    {
        mwmp::mechanics::PlayerProgressionState result;
        for (std::size_t index = 0; index < result.attributes.size(); ++index)
        {
            const ESM::RefId id = ESM::Attribute::indexToRefId(index);
            const auto stat = player.creatureStats.mAttributes.find(id);
            if (stat != player.creatureStats.mAttributes.end())
                result.attributes[index] = progressionStat(stat->second);
            const auto increase = player.npcStats.mSkillIncrease.find(id);
            if (increase != player.npcStats.mSkillIncrease.end())
                result.skillIncreases[index] = increase->second;
        }
        for (std::size_t index = 0; index < result.skills.size(); ++index)
        {
            const ESM::RefId id = ESM::Skill::indexToRefId(index);
            const auto stat = player.npcStats.mSkills.find(id);
            if (stat != player.npcStats.mSkills.end())
                result.skills[index] = progressionStat(stat->second);
        }
        result.level = player.creatureStats.mLevel;
        result.levelProgress = player.npcStats.mLevelProgress;
        return result;
    }

    std::vector<mwmp::mechanics::AttributeProgressionChange> attributeChanges(
        const mwmp::BasePlayer& player)
    {
        std::vector<mwmp::mechanics::AttributeProgressionChange> result;
        result.reserve(player.attributeIndexChanges.size());
        for (const std::uint8_t index : player.attributeIndexChanges)
        {
            if (index >= ESM::Attribute::Length)
            {
                result.push_back({ index, {}, 0 });
                continue;
            }
            const ESM::RefId id = ESM::Attribute::indexToRefId(index);
            const auto stat = player.creatureStats.mAttributes.find(id);
            const auto increase = player.npcStats.mSkillIncrease.find(id);
            result.push_back({ index,
                stat == player.creatureStats.mAttributes.end()
                    ? mwmp::mechanics::ProgressionStat{}
                    : progressionStat(stat->second),
                increase == player.npcStats.mSkillIncrease.end()
                    ? 0 : increase->second });
        }
        return result;
    }

    std::vector<mwmp::mechanics::SkillProgressionChange> skillChanges(
        const mwmp::BasePlayer& player)
    {
        std::vector<mwmp::mechanics::SkillProgressionChange> result;
        result.reserve(player.skillIndexChanges.size());
        for (const std::uint8_t index : player.skillIndexChanges)
        {
            if (index >= ESM::Skill::Length)
            {
                result.push_back({ index, {} });
                continue;
            }
            const ESM::RefId id = ESM::Skill::indexToRefId(index);
            const auto stat = player.npcStats.mSkills.find(id);
            result.push_back({ index,
                stat == player.npcStats.mSkills.end()
                    ? mwmp::mechanics::ProgressionStat{}
                    : progressionStat(stat->second) });
        }
        return result;
    }

    void applyCanonicalAttributes(Player& player,
        const mwmp::mechanics::PlayerProgressionState& state) noexcept
    {
        for (std::size_t index = 0; index < state.attributes.size(); ++index)
        {
            const ESM::RefId id = ESM::Attribute::indexToRefId(index);
            const auto stat = player.creatureStats.mAttributes.find(id);
            if (stat != player.creatureStats.mAttributes.end())
                stat->second = esmProgressionStat(state.attributes[index]);
            const auto increase = player.npcStats.mSkillIncrease.find(id);
            if (increase != player.npcStats.mSkillIncrease.end())
                increase->second = state.skillIncreases[index];
        }
    }

    void applyCanonicalSkills(Player& player,
        const mwmp::mechanics::PlayerProgressionState& state) noexcept
    {
        for (std::size_t index = 0; index < state.skills.size(); ++index)
        {
            const ESM::RefId id = ESM::Skill::indexToRefId(index);
            const auto stat = player.npcStats.mSkills.find(id);
            if (stat != player.npcStats.mSkills.end())
                stat->second = esmProgressionStat(state.skills[index]);
        }
    }
}

bool Networking::validatePlayerAttributes(
    Player& player, const BasePlayer& incoming)
{
    if (!mProgressionLedger.find(player.guid.value))
    {
        const mechanics::ProgressionResult seeded
            = mProgressionLedger.set(player.guid.value, progressionState(player));
        if (!seeded.applied())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                "Failed to seed progression state for connection %llu: %s",
                static_cast<unsigned long long>(player.guid.value),
                mechanics::describe(seeded.decision));
            return false;
        }
    }

    const std::vector<mechanics::AttributeProgressionChange> changes
        = attributeChanges(incoming);
    const mechanics::ProgressionResult result = mProgressionLedger.previewAttributes(
        player.guid.value, incoming.exchangeFullInfo, changes);
    if (result.applied())
    {
        mPendingPlayerAttributes.insert(player.guid.value);
        return true;
    }

    const unsigned int violations = ++mProgressionViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected attribute intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid progression intents");
    return false;
}

bool Networking::commitPlayerAttributes(Player& player)
{
    if (mPendingPlayerAttributes.erase(player.guid.value) == 0)
        return false;
    const std::vector<mechanics::AttributeProgressionChange> changes
        = attributeChanges(player);
    const mechanics::ProgressionResult result = mProgressionLedger.applyAttributes(
        player.guid.value, player.exchangeFullInfo, changes);
    if (result.applied())
    {
        applyCanonicalAttributes(player, result.state);
        return true;
    }

    cancelPlayerAttributeIntent(player);
    const unsigned int violations = ++mProgressionViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified attribute intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid progression intents");
    return false;
}

bool Networking::applyServerPlayerAttributes(Player& player)
{
    if (mPendingPlayerAttributes.contains(player.guid.value))
        return false;
    mechanics::PlayerProgressionState state = mProgressionLedger.find(player.guid.value)
        .value_or(progressionState(player));
    const mechanics::PlayerProgressionState proposed = progressionState(player);
    state.attributes = proposed.attributes;
    state.skillIncreases = proposed.skillIncreases;
    const mechanics::ProgressionResult result
        = mProgressionLedger.set(player.guid.value, std::move(state));
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored attributes for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::isPlayerAttributeIntentPending(
    const Player& player) const noexcept
{
    return mPendingPlayerAttributes.contains(player.guid.value);
}

void Networking::cancelPlayerAttributeIntent(Player& player) noexcept
{
    mPendingPlayerAttributes.erase(player.guid.value);
    if (const auto canonical = mProgressionLedger.find(player.guid.value))
        applyCanonicalAttributes(player, *canonical);
}

bool Networking::validatePlayerSkills(Player& player, const BasePlayer& incoming)
{
    if (!mProgressionLedger.find(player.guid.value))
    {
        const mechanics::ProgressionResult seeded
            = mProgressionLedger.set(player.guid.value, progressionState(player));
        if (!seeded.applied())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                "Failed to seed progression state for connection %llu: %s",
                static_cast<unsigned long long>(player.guid.value),
                mechanics::describe(seeded.decision));
            return false;
        }
    }

    const std::vector<mechanics::SkillProgressionChange> changes
        = skillChanges(incoming);
    const mechanics::ProgressionResult result = mProgressionLedger.previewSkills(
        player.guid.value, incoming.exchangeFullInfo, changes);
    if (result.applied())
    {
        mPendingPlayerSkills.insert(player.guid.value);
        return true;
    }

    const unsigned int violations = ++mProgressionViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected skill intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid progression intents");
    return false;
}

bool Networking::commitPlayerSkills(Player& player)
{
    if (mPendingPlayerSkills.erase(player.guid.value) == 0)
        return false;
    const std::vector<mechanics::SkillProgressionChange> changes
        = skillChanges(player);
    const mechanics::ProgressionResult result = mProgressionLedger.applySkills(
        player.guid.value, player.exchangeFullInfo, changes);
    if (result.applied())
    {
        applyCanonicalSkills(player, result.state);
        return true;
    }

    cancelPlayerSkillIntent(player);
    const unsigned int violations = ++mProgressionViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified skill intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid progression intents");
    return false;
}

bool Networking::applyServerPlayerSkills(Player& player)
{
    if (mPendingPlayerSkills.contains(player.guid.value))
        return false;
    mechanics::PlayerProgressionState state = mProgressionLedger.find(player.guid.value)
        .value_or(progressionState(player));
    state.skills = progressionState(player).skills;
    const mechanics::ProgressionResult result
        = mProgressionLedger.set(player.guid.value, std::move(state));
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored skills for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::isPlayerSkillIntentPending(const Player& player) const noexcept
{
    return mPendingPlayerSkills.contains(player.guid.value);
}

void Networking::cancelPlayerSkillIntent(Player& player) noexcept
{
    mPendingPlayerSkills.erase(player.guid.value);
    if (const auto canonical = mProgressionLedger.find(player.guid.value))
        applyCanonicalSkills(player, *canonical);
}

bool Networking::validatePlayerLevel(Player& player, const BasePlayer& incoming)
{
    if (!mProgressionLedger.find(player.guid.value))
    {
        const mechanics::ProgressionResult seeded
            = mProgressionLedger.set(player.guid.value, progressionState(player));
        if (!seeded.applied())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                "Failed to seed progression state for connection %llu: %s",
                static_cast<unsigned long long>(player.guid.value),
                mechanics::describe(seeded.decision));
            return false;
        }
    }

    const mechanics::ProgressionResult result = mProgressionLedger.previewLevel(
        player.guid.value, incoming.creatureStats.mLevel,
        incoming.npcStats.mLevelProgress);
    if (result.applied())
    {
        mPendingPlayerLevels.insert(player.guid.value);
        return true;
    }

    const unsigned int violations = ++mProgressionViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected level intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid progression intents");
    return false;
}

bool Networking::commitPlayerLevel(Player& player)
{
    if (mPendingPlayerLevels.erase(player.guid.value) == 0)
        return false;
    const mechanics::ProgressionResult result = mProgressionLedger.applyLevel(
        player.guid.value, player.creatureStats.mLevel,
        player.npcStats.mLevelProgress);
    if (result.applied())
    {
        player.creatureStats.mLevel = result.state.level;
        player.npcStats.mLevelProgress = result.state.levelProgress;
        return true;
    }

    cancelPlayerLevelIntent(player);
    const unsigned int violations = ++mProgressionViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected modified level intent from connection %llu: %s (violation %u)",
        static_cast<unsigned long long>(player.guid.value),
        mechanics::describe(result.decision), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid progression intents");
    return false;
}

bool Networking::applyServerPlayerLevel(Player& player)
{
    if (mPendingPlayerLevels.contains(player.guid.value))
        return false;
    mechanics::PlayerProgressionState state = mProgressionLedger.find(player.guid.value)
        .value_or(progressionState(player));
    state.level = player.creatureStats.mLevel;
    state.levelProgress = player.npcStats.mLevelProgress;
    const mechanics::ProgressionResult result
        = mProgressionLedger.set(player.guid.value, std::move(state));
    if (!result.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored level for connection %llu: %s",
            static_cast<unsigned long long>(player.guid.value),
            mechanics::describe(result.decision));
    }
    return result.applied();
}

bool Networking::isPlayerLevelIntentPending(const Player& player) const noexcept
{
    return mPendingPlayerLevels.contains(player.guid.value);
}

void Networking::cancelPlayerLevelIntent(Player& player) noexcept
{
    mPendingPlayerLevels.erase(player.guid.value);
    if (const auto canonical = mProgressionLedger.find(player.guid.value))
    {
        player.creatureStats.mLevel = canonical->level;
        player.npcStats.mLevelProgress = canonical->levelProgress;
    }
}

bool Networking::validatePlayerStats(Player& player, const BasePlayer& incoming)
{
    bool valid = true;
    const mechanics::CombatantId id{ mechanics::CombatantKind::Player, player.guid.value, {} };
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

    const unsigned int violations = ++mCombatViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid dynamic stats from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.value), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid dynamic stats");
    return false;
}

bool Networking::reconcilePlayerStats(Player& player)
{
    const mechanics::CombatantId id{ mechanics::CombatantKind::Player, player.guid.value, {} };
    const auto existing = mCombatResolver.find(id);
    mechanics::CombatantState state = playerCombatState(player, existing, false);
    if (existing)
    {
        applyCanonicalHealth(player, state);
        applyCanonicalFatigue(player, state);
    }
    if (const auto magic = mSpellResolver.findCombatant(id))
        applyCanonicalMagicka(player, *magic);
    if (mCombatResolver.upsert(id, state))
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Failed to reconcile canonical stats for connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.value), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid canonical stats");
    return false;
}

bool Networking::applyServerPlayerStats(Player& player)
{
    const mechanics::CombatantId id{ mechanics::CombatantKind::Player, player.guid.value, {} };
    mechanics::CombatantState state = playerCombatState(
        player, mCombatResolver.find(id), true);
    mechanics::SpellCombatantState magic = playerSpellState(player, state,
        mActiveEffectLedger.snapshot(id), mSpellFatigueBase,
        mSpellFatigueMultiplier, mSpellResolver.findCombatant(id), true);
    if (!mCombatResolver.upsert(id, state)
        || !mSpellResolver.upsertCombatant(id, magic))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Rejected server-authored dynamic stats for connection %llu",
            static_cast<unsigned long long>(player.guid.value));
        return false;
    }
    applyCanonicalHealth(player, state);
    applyCanonicalMagicka(player, magic);
    applyCanonicalFatigue(player, state);
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

    const unsigned int violations = ++mCombatViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid actor stats from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.value), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor stats");
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
        {
            applyCanonicalHealth(actor, state);
            applyCanonicalFatigue(actor, state);
        }
        if (const auto magic = mSpellResolver.findCombatant(id))
            applyCanonicalMagicka(actor, *magic);
        if (!mCombatResolver.upsert(id, state))
        {
            const unsigned int violations = ++mCombatViolations[player.guid.value];
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to reconcile canonical actor stats from connection %llu (violation %u)",
                static_cast<unsigned long long>(player.guid.value), violations);
            if (violations >= 5)
                disconnectTransport({ player.guid.value }, "repeated invalid canonical actor stats");
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
        const auto actorTemplate = mActorMagicRegistry.find(
            Misc::StringUtils::lowerCase(actor.refId));
        mechanics::SpellCombatantState magic;
        const bool hasTemplate = actorTemplate.has_value();
        if (hasTemplate)
        {
            magic = actorCasterSpellState(actor, *actorTemplate, state,
                mActiveEffectLedger.snapshot(id), mSpellFatigueBase,
                mSpellFatigueMultiplier, mSpellResolver.findCombatant(id), true);
        }
        else
        {
            magic = actorTargetSpellState(actor, state,
                mActiveEffectLedger.snapshot(id), mSpellResolver.findCombatant(id), true);
        }
        if (!mCombatResolver.upsert(id, state)
            || !mSpellResolver.upsertCombatant(id, magic))
            return false;
        applyCanonicalHealth(actor, state);
        applyCanonicalMagicka(actor, magic);
        applyCanonicalFatigue(actor, state);
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
            valid = valid && attack.target.guid.value != 0 && attack.target.guid != player.guid;
        else
            valid = valid && (attack.target.refNum != 0 || attack.target.mpNum != 0)
                && !(attack.target.refNum != 0 && attack.target.mpNum != 0);
    }
    if (valid)
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid attack intent from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.value), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid attack intents");
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
        mechanics::CombatantKind::Player, player.guid.value, {} };
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
            || !mAuthenticatedConnections.contains(targetPlayer->guid.value)
            || targetPlayer->cell.getShortDescription() != player.cell.getShortDescription())
        {
            rejectionReason = "the target player is unavailable or in another cell";
            return false;
        }
        targetId = { mechanics::CombatantKind::Player, targetPlayer->guid.value, {} };
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
                valid = valid && attack.target.guid.value != 0;
            else
                valid = valid && (attack.target.refNum != 0 || attack.target.mpNum != 0)
                    && !(attack.target.refNum != 0 && attack.target.mpNum != 0);
        }
    }
    if (valid)
        return true;

    const unsigned int violations = ++mCombatViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected invalid actor attack list from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.value), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated invalid actor attack intents");
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
            || !mAuthenticatedConnections.contains(targetPlayer->guid.value)
            || targetPlayer->cell.getShortDescription()
                != actorList.cell.getShortDescription())
        {
            rejectionReason = "the target player is unavailable or in another cell";
            return false;
        }
        targetId = { mechanics::CombatantKind::Player, targetPlayer->guid.value, {} };
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
            static_cast<unsigned long long>(player.guid.value));
        return;
    }

    const unsigned int violations = ++mCombatViolations[player.guid.value];
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected client-claimed actor death from connection %llu (violation %u)",
        static_cast<unsigned long long>(player.guid.value), violations);
    if (violations >= 5)
        disconnectTransport({ player.guid.value }, "repeated client-claimed actor deaths");
}

persistence::QueueDecision Networking::queuePersistenceWrite(
    std::filesystem::path path, std::string_view contents)
{
    persistence::AtomicWriteOptions options;
    options.backup = persistence::BackupPolicy::MaintainOne;
    options.maximumBytes = 64U * 1024U * 1024U;
    return queuePersistenceWrite(
        std::move(path), std::as_bytes(std::span(contents)), std::move(options));
}

persistence::QueueDecision Networking::queuePersistenceWrite(
    std::filesystem::path path, std::span<const std::byte> contents,
    persistence::AtomicWriteOptions options)
{
    const std::string displayPath = path.generic_string();
    return mPersistenceService.save(std::move(path), contents, std::move(options),
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

bool Networking::installSpellDefinitions(
    const std::vector<mechanics::SpellDefinition>& definitions)
{
    for (const mechanics::SpellDefinition& definition : definitions)
    {
        if (!mSpellResolver.upsertDefinition(definition))
            return false;
    }
    return true;
}

bool Networking::installActorMagicTemplates(
    const std::vector<mechanics::ActorMagicTemplate>& actors)
{
    for (const mechanics::ActorMagicTemplate& actor : actors)
    {
        if (!mActorMagicRegistry.upsert(actor))
            return false;
    }
    return true;
}

void Networking::setConsumableMagicItems(
    std::unordered_set<std::string> itemIds)
{
    mConsumableMagicItems = std::move(itemIds);
}

void Networking::setSpellFatigueFormula(double base, double multiplier)
{
    if (!std::isfinite(base) || !std::isfinite(multiplier))
        throw std::invalid_argument("spell fatigue formula must be finite");
    mSpellFatigueBase = base;
    mSpellFatigueMultiplier = multiplier;
}

void Networking::advanceActiveEffects(double elapsedSeconds)
{
    mechanics::ActiveEffectLedger activeEffects = mActiveEffectLedger;
    const mechanics::ActiveEffectAdvanceResult advanced
        = activeEffects.advance(elapsedSeconds);
    if (!advanced.applied())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Failed to advance canonical active effects: %s",
            mechanics::describe(advanced.decision));
        return;
    }
    if (advanced.changes.empty())
    {
        mActiveEffectLedger.swap(activeEffects);
        return;
    }

    struct AppliedTick
    {
        mechanics::ActiveEffectTick tick;
        bool healthChanged = false;
        bool magickaChanged = false;
        bool fatigueChanged = false;
        bool died = false;
    };
    std::vector<AppliedTick> applied;
    applied.reserve(advanced.changes.size());
    mechanics::CombatResolver combat = mCombatResolver;
    mechanics::SpellResolver spells = mSpellResolver;
    for (const mechanics::ActiveEffectTick& tick : advanced.changes)
    {
        AppliedTick& change = applied.emplace_back();
        change.tick = tick;
        if (tick.healthDelta == 0 && tick.magickaDelta == 0
            && tick.fatigueDelta == 0)
            continue;

        auto state = combat.find(tick.owner);
        if (!state || !state->alive)
            continue;
        const bool wasAlive = state->alive;
        if (tick.healthDelta != 0)
        {
            state->health = std::clamp(state->health + tick.healthDelta,
                0.0, state->maximumHealth);
            change.healthChanged = true;
        }
        if (tick.fatigueDelta != 0)
        {
            state->fatigue = std::clamp(state->fatigue + tick.fatigueDelta,
                0.0, state->maximumFatigue);
            state->fatigueRatio = state->maximumFatigue == 0
                ? 1.0 : state->fatigue / state->maximumFatigue;
            change.fatigueChanged = true;
        }
        state->alive = state->health > 0;
        if (!combat.upsert(tick.owner, *state))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s",
                "Canonical active-effect health update became invalid");
            return;
        }
        if (auto magicState = spells.findCombatant(tick.owner))
        {
            magicState->health = state->health;
            magicState->maximumHealth = state->maximumHealth;
            magicState->fatigue = state->fatigue;
            magicState->maximumFatigue = state->maximumFatigue;
            if (tick.magickaDelta != 0)
            {
                magicState->magicka = std::clamp(
                    magicState->magicka + tick.magickaDelta,
                    0.0, magicState->maximumMagicka);
                change.magickaChanged = true;
            }
            magicState->alive = state->alive;
            if (!spells.upsertCombatant(tick.owner, *magicState))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s",
                    "Canonical active-effect magic state became invalid");
                return;
            }
        }
        change.died = wasAlive && !state->alive;
    }

    mActiveEffectLedger.swap(activeEffects);
    mCombatResolver.swap(combat);
    mSpellResolver.swap(spells);

    for (const AppliedTick& change : applied)
    {
        const auto canonical = mCombatResolver.find(change.tick.owner);
        if (change.tick.owner.kind == mechanics::CombatantKind::Player)
        {
            Player* player = Players::getPlayer(transport::TransportConnectionId(
                change.tick.owner.value));
            if (player == nullptr)
                continue;
            if ((change.healthChanged || change.magickaChanged
                    || change.fatigueChanged) && canonical)
            {
                player->statsDynamicIndexChanges.clear();
                if (change.healthChanged)
                {
                    applyCanonicalHealth(*player, *canonical);
                    player->statsDynamicIndexChanges.push_back(0);
                }
                if (change.magickaChanged)
                {
                    if (const auto magic = mSpellResolver.findCombatant(change.tick.owner))
                        applyCanonicalMagicka(*player, *magic);
                    player->statsDynamicIndexChanges.push_back(1);
                }
                if (change.fatigueChanged)
                {
                    applyCanonicalFatigue(*player, *canonical);
                    player->statsDynamicIndexChanges.push_back(2);
                }
                player->exchangeFullInfo = false;
                PlayerPacket* packet = playerPacketController->GetPacket(
                    ID_PLAYER_STATS_DYNAMIC);
                packet->setPlayer(player);
                packet->Send(player->guid);
                player->sendToLoaded(packet);
            }
            if (change.tick.topologyChanged)
            {
                player->spellsActiveChanges.action = SpellsActiveChanges::SET;
                player->spellsActiveChanges.activeSpells.clear();
                if (const auto snapshot = mActiveEffectLedger.snapshot(
                        change.tick.owner))
                {
                    for (const mechanics::CanonicalActiveSpell& spell : *snapshot)
                        player->spellsActiveChanges.activeSpells.push_back(
                            wireActiveSpell(spell));
                }
                PlayerPacket* packet = playerPacketController->GetPacket(
                    ID_PLAYER_SPELLS_ACTIVE);
                packet->setPlayer(player);
                packet->Send(player->guid);
                player->sendToLoaded(packet);
            }
            if (change.died)
            {
                Target killer;
                if (change.tick.damageSource)
                {
                    if (change.tick.damageSource->kind
                        == mechanics::CombatantKind::Player)
                    {
                        killer.isPlayer = true;
                        killer.guid = transport::TransportConnectionId(
                            change.tick.damageSource->value);
                    }
                    else
                    {
                        killer.refNum = static_cast<unsigned int>(
                            change.tick.damageSource->value >> 32);
                        killer.mpNum = static_cast<unsigned int>(
                            change.tick.damageSource->value);
                        if (Cell* sourceCell = CellController::get()
                                ->getCellByDescription(
                                    change.tick.damageSource->scope))
                        {
                            if (BaseActor* source = sourceCell->getActor(
                                    killer.refNum, killer.mpNum))
                            {
                                killer.refId = source->refId;
                                killer.name = source->refId;
                            }
                        }
                    }
                }
                publishCanonicalPlayerDeath(*player, killer);
            }
            continue;
        }

        Cell* cell = CellController::get()->getCellByDescription(
            change.tick.owner.scope);
        if (cell == nullptr)
            continue;
        const unsigned int refNum = static_cast<unsigned int>(
            change.tick.owner.value >> 32);
        const unsigned int mpNum = static_cast<unsigned int>(
            change.tick.owner.value);
        BaseActor* actor = cell->getActor(refNum, mpNum);
        if (actor == nullptr)
            continue;
        if ((change.healthChanged || change.magickaChanged
                || change.fatigueChanged) && canonical)
        {
            if (change.healthChanged)
                applyCanonicalHealth(*actor, *canonical);
            if (change.magickaChanged)
            {
                if (const auto magic = mSpellResolver.findCombatant(change.tick.owner))
                    applyCanonicalMagicka(*actor, *magic);
            }
            if (change.fatigueChanged)
                applyCanonicalFatigue(*actor, *canonical);
            actor->hasStatsDynamicData = true;
            BaseActorList list;
            list.cell = cell->getActorList()->cell;
            list.authorityLeaseId = cell->getAuthorityLeaseId();
            list.baseActors.push_back(*actor);
            list.count = 1;
            ActorPacket* packet = actorPacketController->GetPacket(
                ID_ACTOR_STATS_DYNAMIC);
            packet->setActorList(&list);
            cell->sendToLoaded(packet, &list);
        }
        if (change.tick.topologyChanged)
        {
            BaseActor activeActor = *actor;
            activeActor.spellsActiveChanges.action = SpellsActiveChanges::SET;
            activeActor.spellsActiveChanges.activeSpells.clear();
            if (const auto snapshot = mActiveEffectLedger.snapshot(
                    change.tick.owner))
            {
                for (const mechanics::CanonicalActiveSpell& spell : *snapshot)
                    activeActor.spellsActiveChanges.activeSpells.push_back(
                        wireActiveSpell(spell));
            }
            BaseActorList list;
            list.cell = cell->getActorList()->cell;
            list.authorityLeaseId = cell->getAuthorityLeaseId();
            list.baseActors.push_back(std::move(activeActor));
            list.count = 1;
            ActorPacket* packet = actorPacketController->GetPacket(
                ID_ACTOR_SPELLS_ACTIVE);
            packet->setActorList(&list);
            cell->sendToLoaded(packet, &list);
        }
        if (change.died)
        {
            BaseActor dead = *actor;
            if (change.tick.damageSource)
            {
                if (change.tick.damageSource->kind
                    == mechanics::CombatantKind::Player)
                {
                    dead.killer.isPlayer = true;
                    dead.killer.guid = transport::TransportConnectionId(
                        change.tick.damageSource->value);
                }
                else
                {
                    dead.killer.refNum = static_cast<unsigned int>(
                        change.tick.damageSource->value >> 32);
                    dead.killer.mpNum = static_cast<unsigned int>(
                        change.tick.damageSource->value);
                    if (Cell* sourceCell = CellController::get()
                            ->getCellByDescription(
                                change.tick.damageSource->scope))
                    {
                        if (BaseActor* source = sourceCell->getActor(
                                dead.killer.refNum, dead.killer.mpNum))
                        {
                            dead.killer.refId = source->refId;
                            dead.killer.name = source->refId;
                        }
                    }
                }
            }
            BaseActorList list;
            list.cell = cell->getActorList()->cell;
            list.authorityLeaseId = cell->getAuthorityLeaseId();
            list.baseActors.push_back(std::move(dead));
            list.count = 1;
            ActorPacket* packet = actorPacketController->GetPacket(ID_ACTOR_DEATH);
            packet->setActorList(&list);
            cell->sendToLoaded(packet, &list);
            baseActorList = list;
            if (Player* authority = Players::getPlayer(*cell->getAuthority()))
            {
                Script::Call<Script::CallbackIdentity("OnActorDeath")>(
                    authority->getId(), change.tick.owner.scope.c_str());
            }
        }
    }
}

void Networking::processSystemPacket(const transport::ReceivedApplicationPacket& packet)
{
    Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
    if (player == nullptr)
        return;
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Rejected deprecated system packet %u after the protocol-11 cutover",
        static_cast<unsigned int>(static_cast<std::uint16_t>(packet.id)));
    kickPlayer(player->guid);
}

void Networking::processPlayerPacket(const transport::ReceivedApplicationPacket& packet)
{
    Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
    if (player == nullptr)
        return;
    const std::string peerAddress
        = mEndpoint.peerAddress(packet.sender).value_or(std::string{ "unknown" });

    PlayerPacket *myPacket = playerPacketController->GetPacket(static_cast<std::uint16_t>(packet.id));

    if (!player->isHandshaked())
    {
        player->incrementHandshakeAttempts();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Have not completed handshake with client at %s", peerAddress.c_str());
        LOG_APPEND(TimedLog::LOG_WARN, "- Attempts so far: %i", player->getHandshakeAttempts());

        if (player->getHandshakeAttempts() > 20)
            kickPlayer(player->guid, false);
        else if (player->getHandshakeAttempts() > 5)
            kickPlayer(player->guid, true);

        return;
    }

    if (static_cast<std::uint16_t>(packet.id) == ID_LOADED)
        player->setLoadState(Player::LOADED);
    else if (static_cast<std::uint16_t>(packet.id) == ID_PLAYER_BASEINFO)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_BASEINFO about %s", player->npc.mName.c_str());

        BasePlayer validation(mwmp::transport::TransportConnectionId(packet.sender.value));
        myPacket->setPlayer(&validation);
        myPacket->Read(packet.payload);
        if (!myPacket->isPacketValid())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Invalid ID_PLAYER_BASEINFO packet from client at %s",
                peerAddress.c_str());
            kickPlayer(player->guid);
            return;
        }
        myPacket->setPlayer(player);
        myPacket->Read(packet.payload);
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
        newPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
        return;
    }


    if (!PlayerProcessor::Process(packet))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", static_cast<std::uint16_t>(packet.id));

}

void Networking::processActorPacket(const transport::ReceivedApplicationPacket& packet)
{
    Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
    if (player == nullptr)
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ActorProcessor::Process(packet, baseActorList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", static_cast<std::uint16_t>(packet.id));

}

void Networking::processObjectPacket(const transport::ReceivedApplicationPacket& packet)
{
    Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
    if (player == nullptr)
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ObjectProcessor::Process(packet, baseObjectList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ObjectPacket with identifier %i has arrived", static_cast<std::uint16_t>(packet.id));

}

void Networking::processWorldstatePacket(const transport::ReceivedApplicationPacket& packet)
{
    Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
    if (player == nullptr)
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!WorldstateProcessor::Process(packet, baseWorldstate))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled WorldstatePacket with identifier %i has arrived", static_cast<std::uint16_t>(packet.id));

}

bool Networking::preInit(const transport::ReceivedApplicationPacket& packet)
{
    if (static_cast<std::uint16_t>(packet.id) != ID_GAME_PREINIT)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Connection %llu sent the wrong first application packet",
            static_cast<unsigned long long>(packet.sender.value));
        mEndpoint.disconnect({ packet.sender.value });
        return false;
    }

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received content manifest from connection %llu",
        static_cast<unsigned long long>(packet.sender.value));
    PacketPreInit::PluginContainer dataFiles;

    PacketPreInit packetPreInit;
    packetPreInit.setChecksums(&dataFiles);
    packetPreInit.Read(packet.payload);

    if (!packetPreInit.isPacketValid() || dataFiles.empty())
    {
        LOG_APPEND(TimedLog::LOG_ERROR, "- Packet was invalid");
        mEndpoint.disconnect({ packet.sender.value });
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
    packetPreInit.SetApplicationPacketDispatcher(&mDispatcher);
    packetPreInit.setGUID(mwmp::transport::TransportConnectionId(packet.sender.value));

    // If the loop above was broken, then the client's data files do not match the server's
    if (dataFileEnforcementState && dataFile != dataFiles.end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Client was not allowed to connect due to incompatible data files");
        packetPreInit.setChecksums(&samples);
        packetPreInit.Send(mwmp::transport::TransportConnectionId(packet.sender.value));
        mEndpoint.disconnect({ packet.sender.value });
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Client was allowed to connect");
        PacketPreInit::PluginContainer tmp;
        packetPreInit.setChecksums(&tmp);
        packetPreInit.Send(mwmp::transport::TransportConnectionId(packet.sender.value));
        return true;
    }

    return false;
}

void Networking::update(const transport::ReceivedApplicationPacket& packet)
{
    if (systemPacketController->ContainsPacket(static_cast<std::uint16_t>(packet.id)))
        processSystemPacket(packet);
    else if (playerPacketController->ContainsPacket(static_cast<std::uint16_t>(packet.id)))
        processPlayerPacket(packet);
    else if (actorPacketController->ContainsPacket(static_cast<std::uint16_t>(packet.id)))
        processActorPacket(packet);
    else if (objectPacketController->ContainsPacket(static_cast<std::uint16_t>(packet.id)))
        processObjectPacket(packet);
    else if (worldstatePacketController->ContainsPacket(static_cast<std::uint16_t>(packet.id)))
        processWorldstatePacket(packet);
    else
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Unhandled protocol-11 packet with identifier %i has arrived",
            static_cast<std::uint16_t>(packet.id));
}

void Networking::newPlayer(mwmp::transport::TransportConnectionId guid)
{
    playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_POSITION)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->RequestData(guid);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Sending info about other players to %lu", guid.value);

    for (const auto& [playerGuid, ownedPlayer] : *players) //sending other players to new player
    {
        // If we are iterating over the new player, don't send the packets below
        if (playerGuid == guid) continue;

        // If an invalid key makes it into the Players map, ignore it
        else if (playerGuid == mwmp::transport::TransportConnectionId{}) continue;

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

void Networking::disconnectPlayer(mwmp::transport::TransportConnectionId guid)
{
    Player *player = Players::getPlayer(guid);
    if (!player)
        return;
    if (mAuthenticatedConnections.contains(guid.value))
    {
        Script::Call<Script::CallbackIdentity("OnPlayerDisconnect")>(player->getId());
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(player);
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(true);
    }
    mAuthorityLeases.releaseOwner(guid.value);
    mAuthorityViolations.erase(guid.value);
    resetPlayerMovement(guid.value);
    mPlayerLifecycle.erase(guid.value);
    mLifecycleViolations.erase(guid.value);
    mEquipmentLedger.erase(guid.value);
    mInventoryLedger.erase({ mechanics::InventoryOwnerKind::Player, guid.value });
    mInventoryViolations.erase(guid.value);
    mSpellbookLedger.erase(guid.value);
    mSpellbookViolations.erase(guid.value);
    mCombatResolver.erase({ mechanics::CombatantKind::Player, guid.value, {} });
    mSpellResolver.eraseCombatant(
        { mechanics::CombatantKind::Player, guid.value, {} });
    mCombatViolations.erase(guid.value);
    mActiveEffectLedger.erase({ mechanics::CombatantKind::Player, guid.value, {} });
    mActiveEffectViolations.erase(guid.value);
    mActorStateViolations.erase(guid.value);
    mCastViolations.erase(guid.value);
    mJusticeLedger.erase(guid.value);
    mJusticeViolations.erase(guid.value);
    mPendingPlayerBounties.erase(guid.value);
    mShapeshiftLedger.erase(guid.value);
    mShapeshiftViolations.erase(guid.value);
    mPendingPlayerShapeshifts.erase(guid.value);
    mProgressionLedger.erase(guid.value);
    mProgressionViolations.erase(guid.value);
    mPendingPlayerAttributes.erase(guid.value);
    mPendingPlayerSkills.erase(guid.value);
    mPendingPlayerLevels.erase(guid.value);
    mObjectViolations.erase(guid.value);
    mPendingObjectPlacements.erase(guid.value);
    mPendingObjectMutations.erase(guid.value);
    mAcceptedActorAiIntents.erase(guid.value);
    mRelayedActorAiIntents.erase(guid.value);
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

std::string Networking::getPeerAddress(mwmp::transport::TransportConnectionId guid) const
{
    return mEndpoint.peerAddress({ guid.value }).value_or(std::string{});
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

    auto nextMetricsReport = std::chrono::steady_clock::now()
        + std::chrono::minutes(1);
    auto activeEffectClock = std::chrono::steady_clock::now();
    auto nextActiveEffectTick = activeEffectClock + std::chrono::milliseconds(100);
    
    while (running && !killLoop)
    {
        const auto tickStarted = std::chrono::steady_clock::now();
#ifndef _WIN32
        sigaction(SIGTERM, &sigIntHandler, NULL);
        sigaction(SIGINT, &sigIntHandler, NULL);
#endif
        if (stdinHasInput())
        {
            const int character = readStdinCharacter();
            if (character == '\n' || character == '\r')
                break;
        }
        if (auto event = mEndpoint.poll(std::chrono::milliseconds(1)))
            processTransportEvent(std::move(*event));
        TimerAPI::Tick();
        mMetrics.observeQueueDepth(mPersistenceService.pending());
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextActiveEffectTick)
        {
            advanceActiveEffects(
                std::chrono::duration<double>(now - activeEffectClock).count());
            activeEffectClock = now;
            nextActiveEffectTick = now + std::chrono::milliseconds(100);
        }
        mMetrics.observeTick(now - tickStarted);
        if (now >= nextMetricsReport)
        {
            mMetrics.setResidentMemoryBytes(metrics::residentMemoryBytes());
            const auto snapshot = mMetrics.snapshot();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Server metrics: tick_p99_us=%llu serialization_p99_us=%llu "
                "queue=%llu queue_max=%llu inbound_messages=%llu inbound_bytes=%llu "
                "outbound_messages=%llu outbound_bytes=%llu resident_bytes=%llu",
                static_cast<unsigned long long>(snapshot.tickP99Microseconds),
                static_cast<unsigned long long>(snapshot.serializationP99Microseconds),
                static_cast<unsigned long long>(snapshot.queueDepth),
                static_cast<unsigned long long>(snapshot.maximumQueueDepth),
                static_cast<unsigned long long>(snapshot.inbound.messages),
                static_cast<unsigned long long>(snapshot.inbound.bytes),
                static_cast<unsigned long long>(snapshot.outbound.messages),
                static_cast<unsigned long long>(snapshot.outbound.bytes),
                static_cast<unsigned long long>(snapshot.residentMemoryBytes));
            nextMetricsReport = now + std::chrono::minutes(1);
        }
    }

    TimerAPI::Terminate();
    return exitCode;
}

void Networking::processTransportEvent(transport::TransportEvent event)
{
    const mwmp::transport::TransportConnectionId guid(event.connection.value);
    if (event.type == transport::TransportEventType::Message)
    {
        mMetrics.recordInbound(event.connection.value,
            protocol::envelopeBytes + event.message.payload.size());
    }
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
            mMetrics.removeConnection(event.connection.value);
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

    const auto state = mEndpoint.state(message.connection);
    if (state == session::State::TransportAuthenticated)
    {
        if (!preInit(application))
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
        Player* player = Players::getPlayer(application.sender);
        if (player == nullptr)
        {
            disconnectTransport(message.connection, "spawn requested without a player slot");
            return;
        }
        PlayerPacket* response = playerPacketController->GetPacket(ID_LOADED);
        response->setPlayer(player);
        if (response->Send(application.sender) == 0)
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
    update(application);
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

    Player* player = Players::getPlayer(mwmp::transport::TransportConnectionId(message.connection.value));
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
    const auto serializationStarted = std::chrono::steady_clock::now();
    if (!security::encodeAuthenticationResponse(response, payload, codecError))
    {
        mMetrics.observeSerialization(
            std::chrono::steady_clock::now() - serializationStarted);
        return false;
    }
    mMetrics.observeSerialization(
        std::chrono::steady_clock::now() - serializationStarted);
    transport::TransportMessage message;
    message.connection = connection;
    message.delivery = transport::DeliveryMode::ReliableOrdered;
    message.lane = transport::MessageLane::System;
    message.messageType
        = static_cast<std::uint16_t>(protocol::MessageType::AuthenticationResult);
    message.subject = connection.value;
    message.sequence = 1;
    message.payload = std::move(payload);
    const std::size_t messageBytes
        = protocol::envelopeBytes + message.payload.size();
    transport::TransportError error;
    const bool sent = mEndpoint.send(std::move(message), error);
    if (sent)
        mMetrics.recordOutbound(connection.value, messageBytes);
    return sent;
}

void Networking::disconnectTransport(
    transport::TransportConnectionId connection, const char* reason)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Disconnecting transport connection %llu: %s",
        static_cast<unsigned long long>(connection.value), reason);
    mEndpoint.disconnect(connection);
}

void Networking::kickPlayer(mwmp::transport::TransportConnectionId guid, bool sendNotification)
{
    (void)sendNotification;
    disconnectTransport({ guid.value }, "kicked by server");
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

int Networking::getAvgPing(transport::TransportConnectionId connection) const
{
    (void)connection;
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
