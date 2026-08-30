#include <components/openmw-mp/Protocol/MessageType.hpp>
#include <components/openmw-mp/Session/SessionState.hpp>

#include <array>
#include <cstdint>
#include <iostream>

namespace
{
    using mwmp::protocol::MessageType;
    using namespace mwmp::session;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "session.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void expectAdvance(SessionState& session, State target)
    {
        EXPECT(session.advance(target) == TransitionResult::Advanced);
        EXPECT(session.state() == target);
    }

    void testTransitions()
    {
        SessionState session(Endpoint::Server);
        EXPECT(session.state() == State::Connected);
        EXPECT(session.advance(State::ContentVerified) == TransitionResult::OutOfOrder);
        EXPECT(session.state() == State::Connected);

        expectAdvance(session, State::TransportAuthenticated);
        EXPECT(session.advance(State::TransportAuthenticated) == TransitionResult::Duplicate);
        EXPECT(session.state() == State::TransportAuthenticated);
        EXPECT(session.advance(State::Spawned) == TransitionResult::OutOfOrder);
        EXPECT(session.state() == State::TransportAuthenticated);

        expectAdvance(session, State::ContentVerified);
        expectAdvance(session, State::AccountAuthenticated);
        expectAdvance(session, State::Spawned);
        expectAdvance(session, State::Disconnecting);
        EXPECT(session.advance(State::Spawned) == TransitionResult::AlreadyDisconnecting);
        EXPECT(session.advance(State::Disconnecting) == TransitionResult::Duplicate);
    }

    SessionState makeSessionAt(Endpoint endpoint, State state)
    {
        SessionState session(endpoint);
        if (state == State::Disconnecting)
        {
            EXPECT(session.advance(state) == TransitionResult::Advanced);
            return session;
        }
        for (std::uint8_t value = static_cast<std::uint8_t>(State::TransportAuthenticated);
             value <= static_cast<std::uint8_t>(state); ++value)
            EXPECT(session.advance(static_cast<State>(value)) == TransitionResult::Advanced);
        return session;
    }

    void testTransitionMatrix()
    {
        constexpr std::array states{
            State::Connected,
            State::TransportAuthenticated,
            State::ContentVerified,
            State::AccountAuthenticated,
            State::Spawned,
            State::Disconnecting,
        };
        for (const State source : states)
        {
            for (const State target : states)
            {
                SessionState session = makeSessionAt(Endpoint::Server, source);
                const TransitionResult result = session.advance(target);
                TransitionResult expected = TransitionResult::OutOfOrder;
                if (target == source)
                    expected = TransitionResult::Duplicate;
                else if (source == State::Disconnecting)
                    expected = TransitionResult::AlreadyDisconnecting;
                else if (target == State::Disconnecting
                    || static_cast<std::uint8_t>(target)
                        == static_cast<std::uint8_t>(source) + 1U)
                    expected = TransitionResult::Advanced;
                EXPECT(result == expected);
                EXPECT(session.state() == (expected == TransitionResult::Advanced ? target : source));
            }
        }
    }

    void testServerWhitelist()
    {
        SessionState session(Endpoint::Server);
        EXPECT(session.receive(MessageType::AccountLogin) == MessageDecision::NotAllowedInState);
        EXPECT(session.receive(MessageType::CombatResult) == MessageDecision::WrongDirection);
        EXPECT(session.receive(0xffff) == MessageDecision::UnknownMessage);

        expectAdvance(session, State::TransportAuthenticated);
        EXPECT(session.receive(MessageType::ContentManifest) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::AccountLogin) == MessageDecision::NotAllowedInState);
        EXPECT(session.receive(MessageType::Ping) == MessageDecision::Allowed);

        expectAdvance(session, State::ContentVerified);
        EXPECT(session.receive(MessageType::ContentManifest) == MessageDecision::NotAllowedInState);
        EXPECT(session.receive(MessageType::AccountLogin) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::AccountRegister) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::AttackIntent) == MessageDecision::NotAllowedInState);

        expectAdvance(session, State::AccountAuthenticated);
        EXPECT(session.receive(MessageType::SpawnReady) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::AccountLogin) == MessageDecision::NotAllowedInState);

        expectAdvance(session, State::Spawned);
        EXPECT(session.receive(MessageType::AttackIntent) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::InventoryActionIntent) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::ActorSimulationUpdate) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::CombatResult) == MessageDecision::WrongDirection);

        expectAdvance(session, State::Disconnecting);
        EXPECT(session.receive(MessageType::Disconnect) == MessageDecision::NotAllowedInState);
        EXPECT(session.receive(MessageType::AttackIntent) == MessageDecision::NotAllowedInState);
    }

    void testClientWhitelist()
    {
        SessionState session(Endpoint::Client);
        expectAdvance(session, State::TransportAuthenticated);
        EXPECT(session.receive(MessageType::ContentRequirements) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::ContentVerificationResult) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::ContentManifest) == MessageDecision::WrongDirection);

        expectAdvance(session, State::ContentVerified);
        EXPECT(session.receive(MessageType::AuthenticationResult) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::InitialStateChunk) == MessageDecision::NotAllowedInState);

        expectAdvance(session, State::AccountAuthenticated);
        EXPECT(session.receive(MessageType::InitialStateChunk) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::InitialStateComplete) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::SpawnResult) == MessageDecision::Allowed);

        expectAdvance(session, State::Spawned);
        EXPECT(session.receive(MessageType::CombatResult) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::InventoryDelta) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::AuthorityLease) == MessageDecision::Allowed);
        EXPECT(session.receive(MessageType::AttackIntent) == MessageDecision::WrongDirection);
    }

    void testEveryMessageHasOneLifecycleBoundary()
    {
        constexpr std::array endpoints{ Endpoint::Client, Endpoint::Server };
        constexpr std::array states{
            State::Connected,
            State::TransportAuthenticated,
            State::ContentVerified,
            State::AccountAuthenticated,
            State::Spawned,
            State::Disconnecting,
        };
        for (const MessageType message : mwmp::protocol::allMessageTypes())
        {
            std::size_t allowedCount = 0;
            for (const Endpoint endpoint : endpoints)
            {
                for (const State state : states)
                {
                    const SessionState session = makeSessionAt(endpoint, state);
                    if (session.receive(message) == MessageDecision::Allowed)
                        ++allowedCount;
                }
            }
            const bool common = message == MessageType::Disconnect || message == MessageType::Ping
                || message == MessageType::Pong;
            EXPECT(allowedCount == (common ? 8U : 1U));
        }
    }
}

int runSessionTests()
{
    testTransitions();
    testTransitionMatrix();
    testServerWhitelist();
    testClientWhitelist();
    testEveryMessageHasOneLifecycleBoundary();
    EXPECT(mwmp::protocol::isKnownMessageType(
        static_cast<std::uint16_t>(MessageType::RespawnResult)));
    EXPECT(!mwmp::protocol::isKnownMessageType(0));
    return sFailures;
}
