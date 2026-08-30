#ifndef OPENMW_MP_SESSION_SESSION_STATE_HPP
#define OPENMW_MP_SESSION_SESSION_STATE_HPP

#include <components/openmw-mp/Protocol/MessageType.hpp>

#include <cstdint>

namespace mwmp::session
{
    enum class Endpoint : std::uint8_t
    {
        Client,
        Server,
    };

    enum class State : std::uint8_t
    {
        Connected,
        TransportAuthenticated,
        ContentVerified,
        AccountAuthenticated,
        Spawned,
        Disconnecting,
    };

    enum class TransitionResult : std::uint8_t
    {
        Advanced,
        Duplicate,
        OutOfOrder,
        AlreadyDisconnecting,
    };

    enum class MessageDecision : std::uint8_t
    {
        Allowed,
        UnknownMessage,
        WrongDirection,
        NotAllowedInState,
    };

    class SessionState
    {
    public:
        explicit SessionState(Endpoint endpoint) noexcept;

        Endpoint endpoint() const noexcept { return mEndpoint; }
        State state() const noexcept { return mState; }
        TransitionResult advance(State target) noexcept;
        MessageDecision receive(std::uint16_t messageType) const noexcept;
        MessageDecision receive(protocol::MessageType messageType) const noexcept;
        MessageDecision send(std::uint16_t messageType) const noexcept;
        MessageDecision send(protocol::MessageType messageType) const noexcept;

    private:
        Endpoint mEndpoint;
        State mState = State::Connected;
    };

    const char* describe(State state) noexcept;
    const char* describe(TransitionResult result) noexcept;
    const char* describe(MessageDecision decision) noexcept;
}

#endif
