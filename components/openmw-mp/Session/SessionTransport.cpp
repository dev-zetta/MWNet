#include "SessionTransport.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace mwmp::session
{
    SessionTransport::SessionTransport(
        std::unique_ptr<transport::ITransport> transport, Endpoint endpoint)
        : mTransport(std::move(transport))
        , mEndpoint(endpoint)
    {
        if (!mTransport)
            throw std::invalid_argument("session transport requires an underlying transport");
    }

    SessionTransport::~SessionTransport() = default;

    bool SessionTransport::listen(
        const transport::ListenOptions& options, transport::TransportError& error)
    {
        error = {};
        if (mEndpoint != Endpoint::Server)
        {
            error = { transport::TransportErrorCode::InvalidConfiguration,
                "a client session transport cannot listen" };
            return false;
        }
        return mTransport->listen(options, error);
    }

    bool SessionTransport::connect(const transport::ConnectOptions& options,
        transport::TransportConnectionId& connection, transport::TransportError& error)
    {
        error = {};
        if (mEndpoint != Endpoint::Client)
        {
            error = { transport::TransportErrorCode::InvalidConfiguration,
                "a server session transport cannot initiate connections" };
            return false;
        }
        return mTransport->connect(options, connection, error);
    }

    bool SessionTransport::send(
        transport::TransportMessage message, transport::TransportError& error)
    {
        error = {};
        MessageDecision decision = MessageDecision::NotAllowedInState;
        {
            std::scoped_lock lock(mMutex);
            const auto found = mSessions.find(message.connection.value);
            if (found != mSessions.end())
                decision = found->second.send(message.messageType);
        }
        if (decision != MessageDecision::Allowed)
        {
            error = { transport::TransportErrorCode::SecurityFailure,
                "outbound " + std::string(describe(decision)) };
            return false;
        }
        return mTransport->send(std::move(message), error);
    }

    std::optional<transport::TransportEvent> SessionTransport::poll(
        std::chrono::milliseconds timeout)
    {
        auto event = mTransport->poll(timeout);
        if (!event)
            return std::nullopt;
        return process(std::move(*event));
    }

    std::optional<transport::TransportEvent> SessionTransport::process(
        transport::TransportEvent event)
    {
        if (event.type == transport::TransportEventType::Connected)
        {
            if (!event.connection)
                return std::nullopt;
            SessionState session(mEndpoint);
            if (session.advance(State::TransportAuthenticated) != TransitionResult::Advanced)
                return reject(event.connection, "failed to establish the authenticated session state");
            bool inserted = false;
            {
                std::scoped_lock lock(mMutex);
                inserted = mSessions.emplace(event.connection.value, std::move(session)).second;
            }
            if (!inserted)
                return reject(event.connection, "duplicate authenticated transport connection");
            return event;
        }
        if (event.type == transport::TransportEventType::Disconnected)
        {
            std::scoped_lock lock(mMutex);
            mSessions.erase(event.connection.value);
            return event;
        }
        if (event.type != transport::TransportEventType::Message)
            return event;
        if (!event.connection || event.message.connection != event.connection)
            return reject(event.connection, "transport message connection identity mismatch");

        MessageDecision decision = MessageDecision::NotAllowedInState;
        {
            std::scoped_lock lock(mMutex);
            const auto found = mSessions.find(event.connection.value);
            if (found != mSessions.end())
                decision = found->second.receive(event.message.messageType);
        }
        if (decision != MessageDecision::Allowed)
            return reject(event.connection, "inbound " + std::string(describe(decision)));
        return event;
    }

    std::optional<transport::TransportEvent> SessionTransport::reject(
        transport::TransportConnectionId connection, std::string detail)
    {
        {
            std::scoped_lock lock(mMutex);
            mSessions.erase(connection.value);
        }
        mTransport->disconnect(connection);
        return transport::TransportEvent{
            transport::TransportEventType::Disconnected, connection, {}, std::move(detail) };
    }

    void SessionTransport::disconnect(transport::TransportConnectionId connection)
    {
        {
            std::scoped_lock lock(mMutex);
            mSessions.erase(connection.value);
        }
        mTransport->disconnect(connection);
    }

    void SessionTransport::shutdown(std::chrono::milliseconds timeout)
    {
        {
            std::scoped_lock lock(mMutex);
            mSessions.clear();
        }
        mTransport->shutdown(timeout);
    }

    TransitionResult SessionTransport::advance(transport::TransportConnectionId connection,
        State target, transport::TransportError& error)
    {
        error = {};
        std::scoped_lock lock(mMutex);
        const auto found = mSessions.find(connection.value);
        if (found == mSessions.end())
        {
            error = { transport::TransportErrorCode::SecurityFailure,
                "session connection does not exist" };
            return TransitionResult::OutOfOrder;
        }
        const TransitionResult result = found->second.advance(target);
        if (result != TransitionResult::Advanced)
            error = { transport::TransportErrorCode::SecurityFailure, describe(result) };
        return result;
    }

    std::optional<State> SessionTransport::state(
        transport::TransportConnectionId connection) const
    {
        std::scoped_lock lock(mMutex);
        const auto found = mSessions.find(connection.value);
        return found == mSessions.end() ? std::nullopt : std::optional(found->second.state());
    }
}
