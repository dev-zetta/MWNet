#ifndef OPENMW_MP_SESSION_SESSION_TRANSPORT_HPP
#define OPENMW_MP_SESSION_SESSION_TRANSPORT_HPP

#include "SessionState.hpp"

#include <components/openmw-mp/Transport/ITransport.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace mwmp::session
{
    class SessionTransport final : public transport::ITransport
    {
    public:
        SessionTransport(std::unique_ptr<transport::ITransport> transport, Endpoint endpoint);
        ~SessionTransport() override;

        SessionTransport(const SessionTransport&) = delete;
        SessionTransport& operator=(const SessionTransport&) = delete;

        bool listen(const transport::ListenOptions& options, transport::TransportError& error) override;
        bool connect(const transport::ConnectOptions& options,
            transport::TransportConnectionId& connection, transport::TransportError& error) override;
        bool send(transport::TransportMessage message, transport::TransportError& error) override;
        std::optional<transport::TransportEvent> poll(std::chrono::milliseconds timeout) override;
        std::optional<std::string> peerAddress(
            transport::TransportConnectionId connection) const override;
        void disconnect(transport::TransportConnectionId connection) override;
        void shutdown(std::chrono::milliseconds timeout) override;

        TransitionResult advance(transport::TransportConnectionId connection, State target,
            transport::TransportError& error);
        std::optional<State> state(transport::TransportConnectionId connection) const;

    private:
        std::optional<transport::TransportEvent> process(transport::TransportEvent event);
        std::optional<transport::TransportEvent> reject(
            transport::TransportConnectionId connection, std::string detail);

        std::unique_ptr<transport::ITransport> mTransport;
        Endpoint mEndpoint;
        mutable std::mutex mMutex;
        std::unordered_map<std::uint64_t, SessionState> mSessions;
    };
}

#endif
