#include <components/openmw-mp/Mechanics/CombatResolver.hpp>
#include <components/openmw-mp/Mechanics/InventoryLedger.hpp>
#include <components/openmw-mp/Mechanics/JusticeLedger.hpp>
#include <components/openmw-mp/Mechanics/PlayerLifecycle.hpp>
#include <components/openmw-mp/Metrics/ProcessMemory.hpp>
#include <components/openmw-mp/Metrics/ServerMetrics.hpp>
#include <components/openmw-mp/Protocol/MessageType.hpp>
#include <components/openmw-mp/Security/AuthenticationMessages.hpp>
#include <components/openmw-mp/Security/AuthenticationRateLimiter.hpp>
#include <components/openmw-mp/Security/ServerAuthenticationService.hpp>
#include <components/openmw-mp/Transport/GameEndpoint.hpp>

#include "SoakMemorySamples.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MWNET_SOAK_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(MWNET_SOAK_ASAN)
#define MWNET_SOAK_ASAN 1
#endif
#if defined(MWNET_SOAK_ASAN)
#if __has_include(<sanitizer/allocator_interface.h>)
#include <sanitizer/allocator_interface.h>
#define MWNET_SOAK_ALLOCATOR_STATS 1
#endif
#endif

namespace
{
    using mwmp::protocol::MessageType;
    using mwmp::session::State;
    using mwmp::session::TransitionResult;
    using mwmp::transport::DeliveryMode;
    using mwmp::transport::MessageLane;
    using mwmp::transport::GameEndpoint;
    using mwmp::transport::TransportConnectionId;
    using mwmp::transport::TransportError;
    using mwmp::transport::TransportEvent;
    using mwmp::transport::TransportEventType;
    using mwmp::transport::TransportMessage;
    using namespace std::chrono_literals;

    constexpr std::size_t sWireEnvelopeBytes = 28;
#if defined(MWNET_SOAK_ASAN)
    constexpr bool sAddressSanitizerEnabled = true;
#else
    constexpr bool sAddressSanitizerEnabled = false;
#endif

    struct Options
    {
        std::size_t cycles = 1;
        std::size_t clients = 2;
        std::uint64_t durationSeconds = 0;
        std::uint32_t latencyMilliseconds = 0;
        std::uint32_t packetLossPercent = 0;
        bool failOnMemoryGrowth = false;
        std::filesystem::path stateDirectory;
        std::filesystem::path metricsOutput;
        std::string commit = "unknown";
    };

    struct Peer
    {
        std::unique_ptr<GameEndpoint> client;
        TransportConnectionId clientConnection;
        TransportConnectionId serverConnection;
        std::filesystem::path trustPath;
        std::string account;
        std::string password;
    };

    [[noreturn]] void fail(std::string message)
    {
        throw std::runtime_error(std::move(message));
    }

    void require(bool condition, std::string_view message)
    {
        if (!condition)
            fail(std::string(message));
    }

    std::size_t parseSize(std::string_view value, std::string_view option)
    {
        std::size_t position = 0;
        unsigned long long parsed = 0;
        try
        {
            parsed = std::stoull(std::string(value), &position);
        }
        catch (const std::exception&)
        {
            fail(std::string(option) + " requires an unsigned integer");
        }
        if (position != value.size())
            fail(std::string(option) + " requires an unsigned integer");
        return static_cast<std::size_t>(parsed);
    }

    Options parseOptions(int argc, char** argv)
    {
        Options options;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            const auto value = [&]() -> std::string_view {
                if (++index >= argc)
                    fail(std::string(argument) + " requires a value");
                return argv[index];
            };
            if (argument == "--cycles")
                options.cycles = parseSize(value(), argument);
            else if (argument == "--clients")
                options.clients = parseSize(value(), argument);
            else if (argument == "--duration-seconds")
                options.durationSeconds = parseSize(value(), argument);
            else if (argument == "--latency-ms")
                options.latencyMilliseconds = static_cast<std::uint32_t>(
                    parseSize(value(), argument));
            else if (argument == "--packet-loss-percent")
                options.packetLossPercent = static_cast<std::uint32_t>(
                    parseSize(value(), argument));
            else if (argument == "--state-dir")
                options.stateDirectory = value();
            else if (argument == "--metrics-output")
                options.metricsOutput = value();
            else if (argument == "--commit")
                options.commit = value();
            else if (argument == "--fail-on-memory-growth")
                options.failOnMemoryGrowth = true;
            else if (argument == "--help" || argument == "-h")
            {
                std::cout
                    << "Usage: mwnet-headless-integration [options]\n"
                    << "  --cycles N                  Full connect/death/disconnect cycles\n"
                    << "  --clients N                 Concurrent clients (2-8)\n"
                    << "  --duration-seconds N        Minimum elapsed run time\n"
                    << "  --latency-ms N              Deterministic delay before messages\n"
                    << "  --packet-loss-percent N     Deterministic unreliable loss (0-100)\n"
                    << "  --state-dir DIR             Retain identities, trust and accounts\n"
                    << "  --metrics-output FILE       Write machine-readable JSON metrics\n"
                    << "  --commit REV                Revision recorded in metrics\n"
                    << "  --fail-on-memory-growth     Fail on a sustained RSS increase\n";
                std::exit(0);
            }
            else
                fail("unknown option: " + std::string(argument));
        }
        require(options.cycles > 0, "--cycles must be positive");
        require(options.clients >= 2 && options.clients <= 8,
            "--clients must be between 2 and 8");
        require(options.packetLossPercent <= 100,
            "--packet-loss-percent must not exceed 100");
        return options;
    }

    std::filesystem::path uniqueTemporaryDirectory()
    {
        return std::filesystem::temp_directory_path()
            / ("mwnet-headless-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    std::optional<TransportEvent> waitForEvent(GameEndpoint& endpoint,
        TransportEventType type, std::optional<TransportConnectionId> connection = std::nullopt,
        std::optional<MessageType> messageType = std::nullopt,
        std::chrono::seconds timeout = 5s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto event = endpoint.poll(10ms);
            if (!event)
                continue;
            if (connection && event->connection != *connection)
                continue;
            if (event->type == TransportEventType::Disconnected
                && type != TransportEventType::Disconnected)
                fail("connection " + std::to_string(event->connection.value)
                    + " disconnected while waiting for a protocol event: " + event->detail);
            if (event->type != type)
                continue;
            if (messageType && event->message.messageType
                    != static_cast<std::uint16_t>(*messageType))
                continue;
            return event;
        }
        return std::nullopt;
    }

    class Scenario
    {
    public:
        Scenario(Options options, std::filesystem::path root)
            : mOptions(std::move(options))
            , mRoot(std::move(root))
            , mAuthentication(mRoot / "accounts", mRoot / "players",
                authenticationLimits())
            , mResidentMemorySamples(mRoot / "resident-memory-samples.bin")
        {
        }

        void run()
        {
            mStarted = std::chrono::steady_clock::now();
            setPhase("initialization");
            try
            {
                runScenarios();
            }
            catch (const std::exception& error)
            {
                const std::string failure = failureContext() + ": " + error.what();
                try
                {
                    evaluateMemoryGrowth();
                    writeMetrics(mScenariosComplete ? "failed" : "incomplete", failure);
                }
                catch (const std::exception& reportError)
                {
                    std::cerr << "Failed to save partial soak metrics: "
                        << reportError.what() << '\n';
                }
                fail(failure);
            }
        }

    private:
        void runScenarios()
        {
            std::filesystem::create_directories(mRoot / "players");
            createLegacyPlayer();
            startServer(mRoot / "server-identity.key");
            mInitialResidentMemory = mwmp::metrics::residentMemoryBytes();
            mPeakResidentMemory = mInitialResidentMemory;

            // Reconnect the same clients. Recreating their worker threads every
            // cycle measures sanitizer thread-history retention as well as the
            // application's live state, unlike eight long-lived game clients.
            std::vector<Peer> peers(mOptions.clients);
            const auto soakStarted = std::chrono::steady_clock::now();
            for (std::size_t cycle = 0;
                 cycle < mOptions.cycles
                    || std::chrono::steady_clock::now() - soakStarted
                        < std::chrono::seconds(mOptions.durationSeconds);
                 ++cycle)
            {
                mCurrentCycle = cycle + 1;
                for (std::size_t index = 0; index < mOptions.clients; ++index)
                {
                    mCurrentClient = index;
                    setPhase("connect");
                    peers[index] = connect(index, cycle == 0,
                        std::move(peers[index].client));
                    mMetrics.observeQueueDepth(index + 1);
                    setPhase("bootstrap");
                    bootstrap(peers[index], index, cycle);
                    setPhase("gameplay");
                    gameplay(peers[index], index, cycle);
                }
                setPhase("disconnect");
                for (std::size_t index = 0; index < peers.size(); ++index)
                {
                    mCurrentClient = index;
                    disconnect(peers[index], true);
                }
                require(mMetrics.snapshot().connections.empty(),
                    "disconnected peers retained connection metrics");
                require(mCreatedClients == mOptions.clients,
                    "reconnect recreated client instances instead of reusing them");
                mMetrics.observeQueueDepth(0);
                setPhase("memory observation");
                observeMemory();
                mCompletedCycles = cycle + 1;
            }
            setPhase("client shutdown");
            for (Peer& peer : peers)
                peer.client->shutdown(1s);
            peers.clear();

            setPhase("legacy migration verification");
            verifyLegacyMigration();
            mCurrentClient = 1;
            setPhase("authentication lockout verification");
            verifyAuthenticationLockout();
            mCurrentClient = 0;
            setPhase("fingerprint mismatch verification");
            verifyFingerprintMismatch();
            require(mMetrics.snapshot().connections.empty(),
                "final scenarios retained connection metrics");
            mScenariosComplete = true;
            setPhase("memory evaluation");
            evaluateMemoryGrowth();
            if (mOptions.failOnMemoryGrowth && mMonotonicMemoryGrowth)
                fail("average resident memory grew by more than 1% after warm-up");
            writeMetrics("passed");
        }

        double elapsedSeconds() const
        {
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now() - mStarted).count();
        }

        void setPhase(std::string_view phase)
        {
            mPhase = phase;
            mPhaseStarted = std::chrono::steady_clock::now();
        }

        std::string failureContext() const
        {
            return "elapsedSeconds=" + std::to_string(elapsedSeconds())
                + " cycle=" + std::to_string(mCurrentCycle)
                + " client=" + std::to_string(mCurrentClient)
                + " phase=" + std::string(mPhase)
                + " phaseElapsedSeconds=" + std::to_string(std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - mPhaseStarted).count())
                + " lastAuthenticationSeconds=" + std::to_string(mLastAuthenticationSeconds)
                + " maximumAuthenticationSeconds=" + std::to_string(mMaximumAuthenticationSeconds);
        }

        static mwmp::security::AuthenticationLimits authenticationLimits()
        {
            mwmp::security::AuthenticationLimits limits;
            // Soak runs intentionally reconnect rapidly. The test-only pre-KDF
            // allowance avoids rate-limiting successful automation while the
            // failure lockout remains at its production value and is tested below.
            limits.preKdfAttemptsPerMinute = 1'000'000.0;
            limits.preKdfBurst = 1'000'000.0;
            return limits;
        }

        void startServer(const std::filesystem::path& identityPath)
        {
            std::string errorText;
            mServer = GameEndpoint::createServer(identityPath, errorText);
            require(mServer != nullptr, "server identity failed: " + errorText);

            mwmp::transport::ListenOptions listen;
            listen.address = "127.0.0.1";
            listen.maximumConnections = mOptions.clients + 2;
            listen.timeouts.handshake = 5s;
            listen.timeouts.read = 30s;
            TransportError error;
            bool listening = false;
            for (std::uint16_t port = 39200; port < 39300 && !listening; ++port)
            {
                listen.port = port;
                listening = mServer->listen(listen, error);
                if (listening)
                    mPort = port;
            }
            require(listening, "headless server failed to listen: " + error.detail);
            mFingerprint = mServer->serverFingerprint().value_or("");
            require(!mFingerprint.empty(), "headless server has no identity fingerprint");
        }

        void createLegacyPlayer()
        {
            std::ofstream output(mRoot / "players" / "Legacy.json", std::ios::binary);
            require(static_cast<bool>(output), "failed to create legacy account fixture");
            output
                << "{\n"
                << "  \"login\":{\"name\":\"Legacy\","
                << "\"passwordHash\":\"0774be374bdab4fb47ad1b85baddc3b9cbaee98d9a7abb34de6a8467afbfd231\","
                << "\"passwordSalt\":\"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-\"},\n"
                << "  \"stats\":{\"level\":1,\"alive\":true}\n"
                << "}\n";
            require(static_cast<bool>(output), "failed to write legacy account fixture");
        }

        Peer connect(std::size_t index, bool expectFirstTrust,
            std::unique_ptr<GameEndpoint> client = {})
        {
            Peer peer;
            peer.trustPath = mRoot / ("trusted-" + std::to_string(index) + ".json");
            peer.account = index == 0 ? "Legacy" : "Headless-" + std::to_string(index);
            peer.password = index == 0 ? "correct horse battery staple"
                                       : "headless password " + std::to_string(index);

            std::string errorText;
            if (client)
                peer.client = std::move(client);
            else
            {
                peer.client = GameEndpoint::createClient(peer.trustPath, errorText);
                ++mCreatedClients;
            }
            require(peer.client != nullptr, "client trust store failed: " + errorText);

            mwmp::transport::ConnectOptions options;
            options.host = "127.0.0.1";
            options.port = mPort;
            options.timeouts.handshake = 5s;
            options.timeouts.read = 30s;
            TransportError error;
            require(peer.client->connect(options, peer.clientConnection, error),
                "headless client failed to connect: " + error.detail);

            bool trustRequested = false;
            bool clientConnected = false;
            std::optional<TransportConnectionId> serverConnection;
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline
                && (!clientConnected || !serverConnection))
            {
                if (auto event = peer.client->poll(5ms);
                    event && event->connection == peer.clientConnection)
                {
                    if (event->type == TransportEventType::TrustRequired)
                    {
                        trustRequested = true;
                        require(event->detail == mFingerprint,
                            "first-use fingerprint does not match server identity");
                        require(peer.client->confirmFingerprint(
                                event->connection, event->detail, error),
                            "failed to persist trusted fingerprint: " + error.detail);
                    }
                    else if (event->type == TransportEventType::Connected)
                        clientConnected = true;
                    else if (event->type == TransportEventType::Disconnected)
                        fail("client disconnected during handshake: " + event->detail);
                }
                if (auto event = mServer->poll(5ms))
                {
                    if (event->type == TransportEventType::Connected)
                        serverConnection = event->connection;
                    else if (event->type == TransportEventType::Disconnected)
                        fail("server disconnected during handshake: " + event->detail);
                }
            }
            require(clientConnected && serverConnection.has_value(),
                "encrypted handshake timed out");
            require(trustRequested == expectFirstTrust,
                expectFirstTrust ? "first connection did not require trust confirmation"
                                 : "trusted reconnect unexpectedly required confirmation");
            peer.serverConnection = *serverConnection;
            require(peer.client->state(peer.clientConnection)
                    == State::TransportAuthenticated,
                "client did not reach TransportAuthenticated");
            require(mServer->state(peer.serverConnection)
                    == State::TransportAuthenticated,
                "server did not reach TransportAuthenticated");
            return peer;
        }

        TransportEvent transmit(GameEndpoint& source,
            TransportConnectionId sourceConnection, GameEndpoint& destination,
            TransportConnectionId destinationConnection, MessageType type,
            std::vector<std::byte> payload = {},
            MessageLane lane = MessageLane::System,
            DeliveryMode delivery = DeliveryMode::ReliableOrdered,
            std::uint64_t subject = 0, std::uint64_t sequence = 0)
        {
            if (mOptions.latencyMilliseconds != 0)
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(mOptions.latencyMilliseconds));

            TransportMessage message;
            message.connection = sourceConnection;
            message.messageType = static_cast<std::uint16_t>(type);
            message.payload = std::move(payload);
            message.lane = lane;
            message.delivery = delivery;
            message.subject = subject;
            message.sequence = sequence;
            const std::size_t bytes = sWireEnvelopeBytes + message.payload.size();
            TransportError error;
            const auto started = std::chrono::steady_clock::now();
            if (!source.send(std::move(message), error))
            {
                const std::string context = transmissionContext(source, sourceConnection,
                    destination, destinationConnection, type);
                // Only inspect additional events after the operation has failed.
                // Normal message delivery must not consume another peer's events.
                const std::string sourceClose = queuedDisconnectDetails(source, sourceConnection);
                const std::string destinationClose
                    = queuedDisconnectDetails(destination, destinationConnection);
                fail("message rejected: " + context + ": " + error.detail
                    + "; source " + sourceClose + "; destination " + destinationClose);
            }
            mMetrics.observeSerialization(std::chrono::steady_clock::now() - started);
            mMetrics.recordOutbound(destinationConnection.value, bytes);

            const auto tickStarted = std::chrono::steady_clock::now();
            std::optional<TransportEvent> event;
            try
            {
                event = waitForEvent(destination, TransportEventType::Message,
                    destinationConnection, type);
            }
            catch (const std::exception& receiveError)
            {
                fail(transmissionContext(source, sourceConnection,
                    destination, destinationConnection, type) + ": " + receiveError.what());
            }
            mMetrics.observeTick(std::chrono::steady_clock::now() - tickStarted);
            if (!event)
                fail("timed out waiting for protocol message: " + transmissionContext(
                    source, sourceConnection, destination, destinationConnection, type));
            mMetrics.recordInbound(destinationConnection.value, bytes);
            return std::move(*event);
        }

        static std::string transmissionContext(GameEndpoint& source,
            TransportConnectionId sourceConnection, GameEndpoint& destination,
            TransportConnectionId destinationConnection, MessageType type)
        {
            const auto describeEndpoint = [](GameEndpoint& endpoint,
                TransportConnectionId connection) {
                const auto state = endpoint.state(connection);
                return std::string(endpoint.role() == mwmp::session::Endpoint::Server
                        ? "server" : "client")
                    + " connection=" + std::to_string(connection.value)
                    + " state=" + (state ? mwmp::session::describe(*state) : "absent");
            };
            return "messageType=" + std::to_string(static_cast<std::uint16_t>(type))
                + " source={" + describeEndpoint(source, sourceConnection)
                + "} destination={" + describeEndpoint(destination, destinationConnection) + "}";
        }

        static std::string queuedDisconnectDetails(GameEndpoint& endpoint,
            TransportConnectionId connection)
        {
            // Failure diagnostics have a fixed bound even if messages are queued.
            for (std::size_t count = 0; count < 64; ++count)
            {
                const auto event = endpoint.poll(0ms);
                if (!event)
                    break;
                if (event->connection == connection
                    && event->type == TransportEventType::Disconnected)
                    return "disconnect=" + event->detail;
            }
            return "disconnect reason unavailable in queued events";
        }

        mwmp::security::ServerAuthenticationResult authenticate(
            mwmp::security::AuthenticationRequest request)
        {
            const std::string_view previousPhase = mPhase;
            setPhase("authentication");
            auto result = mAuthentication.authenticate(std::move(request), "127.0.0.1");
            mLastAuthenticationSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - mPhaseStarted).count();
            mMaximumAuthenticationSeconds = std::max(
                mMaximumAuthenticationSeconds, mLastAuthenticationSeconds);
            setPhase(previousPhase);
            return result;
        }

        void advance(Peer& peer, State state)
        {
            TransportError error;
            require(peer.client->advance(peer.clientConnection, state, error)
                    == TransitionResult::Advanced,
                "client lifecycle transition failed: " + error.detail);
            require(mServer->advance(peer.serverConnection, state, error)
                    == TransitionResult::Advanced,
                "server lifecycle transition failed: " + error.detail);
        }

        void bootstrap(Peer& peer, std::size_t index, std::size_t cycle)
        {
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::ContentRequirements);
            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::ContentManifest);
            advance(peer, State::ContentVerified);

            std::string passwordError;
            auto password = mwmp::security::PasswordBuffer::copyFrom(
                peer.password, passwordError);
            require(password.has_value(), "failed to prepare authentication password");
            const auto operation = cycle == 0 && index != 0
                ? mwmp::security::AuthenticationOperation::Register
                : mwmp::security::AuthenticationOperation::Login;
            std::vector<std::byte> encoded;
            mwmp::protocol::CodecError codecError;
            require(mwmp::security::encodeAuthenticationRequest(operation,
                    peer.account, *password, nullptr, encoded, codecError),
                "failed to encode authentication request");
            const MessageType requestType
                = operation == mwmp::security::AuthenticationOperation::Register
                ? MessageType::AccountRegister : MessageType::AccountLogin;
            const auto requestEvent = transmit(*peer.client, peer.clientConnection,
                *mServer, peer.serverConnection, requestType, std::move(encoded));

            mwmp::security::AuthenticationRequest request;
            require(static_cast<bool>(mwmp::security::decodeAuthenticationRequest(
                    requestEvent.message.payload, request)),
                "server rejected a valid authentication request");
            auto result = authenticate(std::move(request));
            require(result.response.authenticated(),
                "authentication failed: " + result.response.message);
            if (cycle == 0 && index == 0)
                require(result.legacyMaterialRemoved,
                    "legacy credentials were not durably migrated");
            if (cycle == 0 && index != 0)
                require(result.isNewAccount, "registration did not create an account");

            require(mwmp::security::encodeAuthenticationResponse(
                    result.response, encoded, codecError),
                "failed to encode authentication response");
            const auto responseEvent = transmit(*mServer, peer.serverConnection,
                *peer.client, peer.clientConnection,
                MessageType::AuthenticationResult, std::move(encoded));
            mwmp::security::AuthenticationResponse response;
            require(static_cast<bool>(mwmp::security::decodeAuthenticationResponse(
                    responseEvent.message.payload, response)) && response.authenticated(),
                "client rejected a valid authentication response");

            advance(peer, State::AccountAuthenticated);
            TransportError error;
            require(peer.client->advance(peer.clientConnection,
                    State::AccountAuthenticated, error) == TransitionResult::Duplicate,
                "duplicate client initialization was not rejected idempotently");
            require(mServer->advance(peer.serverConnection,
                    State::AccountAuthenticated, error) == TransitionResult::Duplicate,
                "duplicate server initialization was not rejected idempotently");

            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::InitialStateComplete);
            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::SpawnReady);
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::SpawnResult);
            advance(peer, State::Spawned);
        }

        static mwmp::mechanics::CombatantState combatant(
            double health, mwmp::mechanics::Position3 position)
        {
            mwmp::mechanics::CombatantState state;
            state.health = health;
            state.maximumHealth = health;
            state.fatigue = 100;
            state.maximumFatigue = 100;
            state.fatigueRatio = 1;
            state.accuracy = 0.9;
            state.evasion = 0;
            state.armorRating = 0;
            state.minimumDamage = 20;
            state.maximumDamage = 20;
            state.meleeReach = 128;
            state.projectileReach = 4096;
            state.position = position;
            state.alive = true;
            return state;
        }

        bool dropUnreliable(std::size_t index, std::size_t cycle)
        {
            if (mOptions.packetLossPercent == 0)
                return false;
            const std::uint64_t sample = (mLossSequence++ * 37 + index * 17 + cycle * 13) % 100;
            return sample < mOptions.packetLossPercent;
        }

        void gameplay(Peer& peer, std::size_t index, std::size_t cycle)
        {
            const std::uint64_t sequence = cycle + 1;
            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::ChatIntent,
                { std::byte{ 'h' }, std::byte{ 'i' } }, MessageLane::Player,
                DeliveryMode::ReliableOrdered, peer.serverConnection.value, sequence);
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::ChatResult,
                { std::byte{ 'h' }, std::byte{ 'i' } }, MessageLane::Player,
                DeliveryMode::ReliableOrdered, peer.serverConnection.value, sequence);

            if (!dropUnreliable(index, cycle))
            {
                (void)transmit(*peer.client, peer.clientConnection, *mServer,
                    peer.serverConnection, MessageType::MovementSnapshot, {},
                    MessageLane::Player, DeliveryMode::Unreliable,
                    peer.serverConnection.value, sequence);
            }
            else
                ++mDroppedSnapshots;

            const mwmp::mechanics::CombatantId player{
                mwmp::mechanics::CombatantKind::Player,
                peer.serverConnection.value, {} };
            const mwmp::mechanics::CombatantId actor{
                mwmp::mechanics::CombatantKind::Actor,
                1 + cycle * mOptions.clients + index, "Headless cell" };
            require(mCombat.upsert(player, combatant(100, { 0, 0, 0 })),
                "server rejected canonical player combat state");
            require(mCombat.upsert(actor, combatant(10, { 64, 0, 0 })),
                "server rejected canonical actor combat state");
            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::AttackIntent, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                actor.value, sequence);
            const auto combat = mCombat.resolve(
                { player, actor, sequence, mwmp::mechanics::AttackKind::Melee, 1 }, 0);
            require(combat.applied() && combat.targetDied,
                "server did not resolve canonical combat outcome");
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::CombatResult, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                actor.value, sequence);
            require(mCombat.erase(actor),
                "server failed to release departed canonical actor state");

            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::InventoryActionIntent, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                peer.serverConnection.value, sequence);
            mwmp::mechanics::InventoryItem item;
            item.refId = "gold_001";
            item.count = 1;
            require(mInventory.apply(
                    { mwmp::mechanics::InventoryOwnerKind::Player,
                        peer.serverConnection.value },
                    mwmp::mechanics::InventoryAction::Add, { item }).applied(),
                "server rejected canonical inventory action");
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::InventoryDelta, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                peer.serverConnection.value, sequence);

            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::JailDecisionIntent, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                peer.serverConnection.value, sequence);
            require(mJustice.setBounty(peer.serverConnection.value, 108).applied(),
                "server rejected canonical bounty");
            const auto sentence = mJustice.beginSentence(peer.serverConnection.value,
                5, false, false, "Serving sentence", "Released");
            require(sentence.applied() && sentence.state.sentence,
                "server rejected canonical jail sentence");
            require(mJustice.completeSentence(peer.serverConnection.value,
                    sentence.state.sentence->id, true).applied(),
                "server failed to complete canonical jail sentence");
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::JailResult, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                peer.serverConnection.value, sequence);

            require(mLifecycle.reportDeath(peer.serverConnection.value).applied(),
                "server failed to own death transition");
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::DeathResult, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                peer.serverConnection.value, sequence);
            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::RespawnRequest, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                peer.serverConnection.value, sequence);
            require(mLifecycle.beginRespawn(peer.serverConnection.value, 1).applied(),
                "server failed to begin respawn");
            require(mLifecycle.acknowledgeRespawn(
                    peer.serverConnection.value, 1).applied(),
                "server failed to finish respawn");
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::RespawnResult, {},
                MessageLane::Player, DeliveryMode::ReliableOrdered,
                peer.serverConnection.value, sequence);
        }

        void disconnect(Peer& peer, bool reconnect = false)
        {
            peer.client->disconnect(peer.clientConnection);
            require(waitForEvent(*mServer, TransportEventType::Disconnected,
                    peer.serverConnection).has_value(),
                "server did not observe client disconnect");
            if (!reconnect)
                peer.client->shutdown(1s);
            mLifecycle.erase(peer.serverConnection.value);
            mCombat.erase({ mwmp::mechanics::CombatantKind::Player,
                peer.serverConnection.value, {} });
            mInventory.erase({ mwmp::mechanics::InventoryOwnerKind::Player,
                peer.serverConnection.value });
            mJustice.erase(peer.serverConnection.value);
            // transmit() accounts for both endpoints using destination-local IDs.
            // A client ID differs from its server ID and changes on every reconnect.
            mMetrics.removeConnection(peer.clientConnection.value);
            mMetrics.removeConnection(peer.serverConnection.value);
        }

        void verifyLegacyMigration()
        {
            std::ifstream input(mRoot / "players" / "Legacy.json", std::ios::binary);
            const std::string contents{
                std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
            require(contents.find("passwordHash") == std::string::npos
                    && contents.find("passwordSalt") == std::string::npos,
                "legacy player record retained obsolete credential material");
            require(contents.find("\"schemaVersion\":1") != std::string::npos,
                "legacy player record was not schema-versioned");
        }

        void verifyAuthenticationLockout()
        {
            Peer peer = connect(1, false);
            (void)transmit(*mServer, peer.serverConnection, *peer.client,
                peer.clientConnection, MessageType::ContentRequirements);
            (void)transmit(*peer.client, peer.clientConnection, *mServer,
                peer.serverConnection, MessageType::ContentManifest);
            advance(peer, State::ContentVerified);

            for (std::size_t attempt = 0; attempt < 6; ++attempt)
            {
                std::string passwordError;
                const std::string candidate
                    = attempt < 5 ? "incorrect password" : peer.password;
                auto password = mwmp::security::PasswordBuffer::copyFrom(
                    candidate, passwordError);
                require(password.has_value(), "failed to prepare lockout password");
                std::vector<std::byte> encoded;
                mwmp::protocol::CodecError codecError;
                const bool encodedRequest = mwmp::security::encodeAuthenticationRequest(
                    mwmp::security::AuthenticationOperation::Login,
                    peer.account, *password, nullptr, encoded, codecError);
                require(encodedRequest,
                    "failed to encode lockout request: "
                        + std::string(mwmp::protocol::describe(codecError)));
                const auto event = transmit(*peer.client, peer.clientConnection,
                    *mServer, peer.serverConnection, MessageType::AccountLogin,
                    std::move(encoded));
                mwmp::security::AuthenticationRequest request;
                require(static_cast<bool>(mwmp::security::decodeAuthenticationRequest(
                        event.message.payload, request)),
                    "failed to decode lockout request");
                const auto result = authenticate(std::move(request));
                const auto expected = attempt < 5
                    ? mwmp::security::AuthenticationResponseStatus::InvalidCredentials
                    : mwmp::security::AuthenticationResponseStatus::RateLimited;
                require(result.response.status == expected,
                    "authentication lockout returned an unexpected status");
                require(mwmp::security::encodeAuthenticationResponse(
                        result.response, encoded, codecError),
                    "failed to encode lockout response");
                (void)transmit(*mServer, peer.serverConnection, *peer.client,
                    peer.clientConnection, MessageType::AuthenticationResult,
                    std::move(encoded));
            }
            disconnect(peer);
        }

        void verifyFingerprintMismatch()
        {
            mServer->shutdown(1s);
            std::string errorText;
            auto replacement = GameEndpoint::createServer(
                mRoot / "replacement-identity.key", errorText);
            require(replacement != nullptr,
                "replacement server identity failed: " + errorText);
            mwmp::transport::ListenOptions listen;
            listen.address = "127.0.0.1";
            listen.port = mPort;
            listen.maximumConnections = 1;
            listen.timeouts.handshake = 5s;
            TransportError error;
            require(replacement->listen(listen, error),
                "replacement server failed to listen: " + error.detail);

            auto client = GameEndpoint::createClient(
                mRoot / "trusted-0.json", errorText);
            require(client != nullptr, "failed to reload trusted server record");
            mwmp::transport::ConnectOptions connect;
            connect.host = "127.0.0.1";
            connect.port = mPort;
            connect.timeouts.handshake = 5s;
            TransportConnectionId connection;
            require(client->connect(connect, connection, error),
                "mismatch client failed to initiate connection: " + error.detail);

            bool blocked = false;
            bool insecureFallback = false;
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline && !blocked)
            {
                if (auto event = client->poll(5ms))
                {
                    blocked = event->type == TransportEventType::Disconnected
                        && event->detail.find("fingerprint mismatch") != std::string::npos;
                    insecureFallback = insecureFallback
                        || event->type == TransportEventType::TrustRequired
                        || event->type == TransportEventType::Connected;
                }
                (void)replacement->poll(5ms);
            }
            require(blocked && !insecureFallback,
                "fingerprint mismatch did not fail closed");
            client->shutdown(1s);
            replacement->shutdown(1s);
        }

        void observeMemory()
        {
            const std::uint64_t current = mwmp::metrics::residentMemoryBytes();
            mResidentMemorySamples.append(current);
            mPeakResidentMemory = std::max(mPeakResidentMemory, current);
            if (mResidentMemorySamples.size() % 100 == 0)
            {
                std::cout << "Soak progress: cycles=" << mResidentMemorySamples.size()
                    << " elapsedSeconds=" << elapsedSeconds()
                    << " residentBytes=" << current
                    << " clientInstances=" << mCreatedClients
                    << " lastAuthenticationSeconds=" << mLastAuthenticationSeconds
                    << " maximumAuthenticationSeconds=" << mMaximumAuthenticationSeconds;
#if defined(MWNET_SOAK_ALLOCATOR_STATS)
                std::cout << " liveAllocatedBytes=" << __sanitizer_get_current_allocated_bytes()
                    << " allocatorHeapBytes=" << __sanitizer_get_heap_size();
#endif
                std::cout << std::endl;
            }
        }

        void evaluateMemoryGrowth()
        {
            mResidentMemoryGrowthPercent = mResidentMemorySamples.growthPercentAfterWarmup();
            mMonotonicMemoryGrowth = mResidentMemoryGrowthPercent > 1.0;
        }

        static std::string jsonEscape(std::string_view value)
        {
            std::string result;
            for (const char character : value)
            {
                if (character == '\\' || character == '"')
                    result.push_back('\\');
                else if (static_cast<unsigned char>(character) < 0x20)
                {
                    constexpr std::string_view hex = "0123456789abcdef";
                    result += "\\u00";
                    result.push_back(hex[static_cast<unsigned char>(character) >> 4]);
                    result.push_back(hex[static_cast<unsigned char>(character) & 15]);
                    continue;
                }
                result.push_back(character);
            }
            return result;
        }

        void writeMetrics(std::string_view status, std::string_view failure = {})
        {
            mMetrics.setResidentMemoryBytes(mwmp::metrics::residentMemoryBytes());
            const auto snapshot = mMetrics.snapshot();
            if (status == "passed")
                std::cout << "Headless MWNet scenarios passed: "
                          << mCompletedCycles << " cycles, " << mOptions.clients
                          << " clients, " << mDroppedSnapshots << " dropped snapshots.\n";
            if (mOptions.metricsOutput.empty())
                return;
            const auto parent = mOptions.metricsOutput.parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
            std::ofstream output(mOptions.metricsOutput, std::ios::binary | std::ios::trunc);
            require(static_cast<bool>(output), "failed to create metrics report");
            output
                << "{\n"
                << "  \"schemaVersion\": 1,\n"
                << "  \"status\": \"" << status << "\",\n"
                << "  \"scenariosComplete\": " << (mScenariosComplete ? "true" : "false") << ",\n"
                << "  \"failure\": \"" << jsonEscape(failure) << "\",\n"
                << "  \"elapsedSeconds\": " << elapsedSeconds() << ",\n"
                << "  \"currentCycle\": " << mCurrentCycle << ",\n"
                << "  \"currentClient\": " << mCurrentClient << ",\n"
                << "  \"phase\": \"" << mPhase << "\",\n"
                << "  \"lastAuthenticationSeconds\": " << mLastAuthenticationSeconds << ",\n"
                << "  \"maximumAuthenticationSeconds\": " << mMaximumAuthenticationSeconds << ",\n"
                << "  \"addressSanitizerEnabled\": "
                << (sAddressSanitizerEnabled ? "true" : "false") << ",\n"
                << "  \"commit\": \"" << jsonEscape(mOptions.commit) << "\",\n"
                << "  \"cycles\": " << mCompletedCycles << ",\n"
                << "  \"clients\": " << mOptions.clients << ",\n"
                << "  \"simulatedLatencyMilliseconds\": "
                << mOptions.latencyMilliseconds << ",\n"
                << "  \"simulatedPacketLossPercent\": "
                << mOptions.packetLossPercent << ",\n"
                << "  \"droppedSnapshots\": " << mDroppedSnapshots << ",\n"
                << "  \"tickP99Microseconds\": "
                << snapshot.tickP99Microseconds << ",\n"
                << "  \"serializationP99Microseconds\": "
                << snapshot.serializationP99Microseconds << ",\n"
                << "  \"maximumQueueDepth\": "
                << snapshot.maximumQueueDepth << ",\n"
                << "  \"residentMemoryBytes\": "
                << snapshot.residentMemoryBytes << ",\n"
                << "  \"initialResidentMemoryBytes\": "
                << mInitialResidentMemory << ",\n"
                << "  \"peakResidentMemoryBytes\": "
                << mPeakResidentMemory << ",\n"
                << "  \"monotonicMemoryGrowthDetected\": "
                << (mMonotonicMemoryGrowth ? "true" : "false") << ",\n"
                << "  \"residentMemoryGrowthPercentAfterWarmup\": "
                << mResidentMemoryGrowthPercent << ",\n"
                << "  \"residentMemorySamplesBytes\": ";
            mResidentMemorySamples.writeJsonArray(output);
            output << ",\n"
                << "  \"inboundBytes\": " << snapshot.inbound.bytes << ",\n"
                << "  \"outboundBytes\": " << snapshot.outbound.bytes << "\n"
                << "}\n";
            require(static_cast<bool>(output), "failed to write metrics report");
        }

        Options mOptions;
        std::filesystem::path mRoot;
        std::unique_ptr<GameEndpoint> mServer;
        std::uint16_t mPort = 0;
        std::string mFingerprint;
        mwmp::security::ServerAuthenticationService mAuthentication;
        mwmp::mechanics::CombatResolver mCombat;
        mwmp::mechanics::InventoryLedger mInventory;
        mwmp::mechanics::JusticeLedger mJustice;
        mwmp::mechanics::PlayerLifecycle mLifecycle;
        mwmp::metrics::ServerMetrics mMetrics;
        std::uint64_t mLossSequence = 0;
        std::uint64_t mDroppedSnapshots = 0;
        std::chrono::steady_clock::time_point mStarted;
        std::chrono::steady_clock::time_point mPhaseStarted;
        std::string_view mPhase = "not started";
        std::size_t mCurrentCycle = 0;
        std::size_t mCurrentClient = 0;
        double mLastAuthenticationSeconds = 0;
        double mMaximumAuthenticationSeconds = 0;
        bool mScenariosComplete = false;
        std::size_t mCompletedCycles = 0;
        std::size_t mCreatedClients = 0;
        std::uint64_t mInitialResidentMemory = 0;
        std::uint64_t mPeakResidentMemory = 0;
        bool mMonotonicMemoryGrowth = false;
        double mResidentMemoryGrowthPercent = 0;
        mwnet::tests::SoakMemorySamples mResidentMemorySamples;
    };
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--build-info")
        {
            std::cout << "{\"addressSanitizerEnabled\":"
                << (sAddressSanitizerEnabled ? "true" : "false") << "}\n";
            return 0;
        }
        Options options = parseOptions(argc, argv);
        const bool temporary = options.stateDirectory.empty();
        const std::filesystem::path root
            = temporary ? uniqueTemporaryDirectory() : options.stateDirectory;
        {
            Scenario scenario(std::move(options), root);
            scenario.run();
        }
        if (temporary)
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "mwnet-headless-integration: " << error.what() << '\n';
        return 1;
    }
}
