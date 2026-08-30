#include "SessionState.hpp"

#include <array>

namespace mwmp::session
{
    namespace
    {
        using protocol::MessageType;

        constexpr std::array sCommonMessages{
            MessageType::Disconnect,
            MessageType::Ping,
            MessageType::Pong,
        };

        constexpr std::array sClientContentMessages{
            MessageType::ContentRequirements,
            MessageType::ContentVerificationResult,
        };
        constexpr std::array sServerContentMessages{ MessageType::ContentManifest };
        constexpr std::array sClientAuthenticationMessages{ MessageType::AuthenticationResult };
        constexpr std::array sServerAuthenticationMessages{
            MessageType::AccountLogin,
            MessageType::AccountRegister,
        };
        constexpr std::array sClientInitializationMessages{
            MessageType::InitialStateChunk,
            MessageType::InitialStateComplete,
            MessageType::SpawnResult,
        };
        constexpr std::array sServerInitializationMessages{ MessageType::SpawnReady };
        constexpr std::array sClientGameplayMessages{
            MessageType::ServerNotice,
            MessageType::ChatResult,
            MessageType::CommandResult,
            MessageType::PlayerStateSnapshot,
            MessageType::CombatResult,
            MessageType::InventoryDelta,
            MessageType::DeathResult,
            MessageType::RespawnResult,
            MessageType::JailResult,
            MessageType::AuthorityLease,
            MessageType::ActorStateSnapshot,
            MessageType::ActorCombatResult,
            MessageType::ObjectStateUpdate,
            MessageType::ContainerDelta,
            MessageType::WorldstateUpdate,
        };
        constexpr std::array sServerGameplayMessages{
            MessageType::ChatIntent,
            MessageType::CommandIntent,
            MessageType::MovementSnapshot,
            MessageType::AttackIntent,
            MessageType::CastIntent,
            MessageType::InventoryActionIntent,
            MessageType::ItemUseIntent,
            MessageType::RespawnRequest,
            MessageType::JailDecisionIntent,
            MessageType::ActorSimulationUpdate,
            MessageType::ObjectActivateIntent,
            MessageType::ObjectChangeIntent,
            MessageType::ContainerActionIntent,
            MessageType::WorldstateIntent,
        };

        template <std::size_t Size>
        constexpr bool contains(const std::array<MessageType, Size>& messages, MessageType message)
        {
            for (const MessageType allowed : messages)
            {
                if (allowed == message)
                    return true;
            }
            return false;
        }

        bool hasCorrectDirection(Endpoint endpoint, MessageType message) noexcept
        {
            if (contains(sCommonMessages, message))
                return true;
            if (endpoint == Endpoint::Client)
                return contains(sClientContentMessages, message)
                    || contains(sClientAuthenticationMessages, message)
                    || contains(sClientInitializationMessages, message)
                    || contains(sClientGameplayMessages, message);
            return contains(sServerContentMessages, message)
                || contains(sServerAuthenticationMessages, message)
                || contains(sServerInitializationMessages, message)
                || contains(sServerGameplayMessages, message);
        }

        bool allowedInState(Endpoint endpoint, State state, MessageType message) noexcept
        {
            if (state == State::Connected || state == State::Disconnecting)
                return false;
            if (contains(sCommonMessages, message))
                return true;
            switch (state)
            {
                case State::TransportAuthenticated:
                    return endpoint == Endpoint::Client
                        ? contains(sClientContentMessages, message)
                        : contains(sServerContentMessages, message);
                case State::ContentVerified:
                    return endpoint == Endpoint::Client
                        ? contains(sClientAuthenticationMessages, message)
                        : contains(sServerAuthenticationMessages, message);
                case State::AccountAuthenticated:
                    return endpoint == Endpoint::Client
                        ? contains(sClientInitializationMessages, message)
                        : contains(sServerInitializationMessages, message);
                case State::Spawned:
                    return endpoint == Endpoint::Client
                        ? contains(sClientGameplayMessages, message)
                        : contains(sServerGameplayMessages, message);
                case State::Connected:
                case State::Disconnecting:
                    return false;
            }
            return false;
        }
    }

    SessionState::SessionState(Endpoint endpoint) noexcept
        : mEndpoint(endpoint)
    {
    }

    TransitionResult SessionState::advance(State target) noexcept
    {
        if (target == mState)
            return TransitionResult::Duplicate;
        if (mState == State::Disconnecting)
            return TransitionResult::AlreadyDisconnecting;
        if (target == State::Disconnecting)
        {
            mState = target;
            return TransitionResult::Advanced;
        }
        if (static_cast<std::uint8_t>(target) != static_cast<std::uint8_t>(mState) + 1U)
            return TransitionResult::OutOfOrder;
        mState = target;
        return TransitionResult::Advanced;
    }

    MessageDecision SessionState::receive(std::uint16_t messageType) const noexcept
    {
        if (!protocol::isKnownMessageType(messageType))
            return MessageDecision::UnknownMessage;
        return receive(static_cast<protocol::MessageType>(messageType));
    }

    MessageDecision SessionState::receive(protocol::MessageType messageType) const noexcept
    {
        if (!hasCorrectDirection(mEndpoint, messageType))
            return MessageDecision::WrongDirection;
        return allowedInState(mEndpoint, mState, messageType)
            ? MessageDecision::Allowed
            : MessageDecision::NotAllowedInState;
    }

    const char* describe(State state) noexcept
    {
        switch (state)
        {
            case State::Connected:
                return "Connected";
            case State::TransportAuthenticated:
                return "TransportAuthenticated";
            case State::ContentVerified:
                return "ContentVerified";
            case State::AccountAuthenticated:
                return "AccountAuthenticated";
            case State::Spawned:
                return "Spawned";
            case State::Disconnecting:
                return "Disconnecting";
        }
        return "Unknown";
    }

    const char* describe(TransitionResult result) noexcept
    {
        switch (result)
        {
            case TransitionResult::Advanced:
                return "transition accepted";
            case TransitionResult::Duplicate:
                return "duplicate transition";
            case TransitionResult::OutOfOrder:
                return "out-of-order transition";
            case TransitionResult::AlreadyDisconnecting:
                return "session is disconnecting";
        }
        return "unknown transition result";
    }

    const char* describe(MessageDecision decision) noexcept
    {
        switch (decision)
        {
            case MessageDecision::Allowed:
                return "message allowed";
            case MessageDecision::UnknownMessage:
                return "unknown protocol-11 message";
            case MessageDecision::WrongDirection:
                return "message is invalid for this endpoint";
            case MessageDecision::NotAllowedInState:
                return "message is not allowed in the current session state";
        }
        return "unknown message decision";
    }
}
