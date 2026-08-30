#ifndef OPENMW_MP_TRANSPORT_PROTOCOL11_ENDPOINT_HPP
#define OPENMW_MP_TRANSPORT_PROTOCOL11_ENDPOINT_HPP

#include "ITransport.hpp"

#include <components/openmw-mp/Session/SessionState.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace mwmp::session
{
    class SessionTransport;
}

namespace mwmp::transport
{
    class SecureTransport;

    class Protocol11Endpoint
    {
    public:
        static std::unique_ptr<Protocol11Endpoint> createServer(
            const std::filesystem::path& identityPath, std::string& error);
        static std::unique_ptr<Protocol11Endpoint> createClient(
            const std::filesystem::path& trustStorePath, std::string& error);

        ~Protocol11Endpoint();

        Protocol11Endpoint(const Protocol11Endpoint&) = delete;
        Protocol11Endpoint& operator=(const Protocol11Endpoint&) = delete;

        bool listen(const ListenOptions& options, TransportError& error);
        bool connect(const ConnectOptions& options,
            TransportConnectionId& connection, TransportError& error);
        bool send(TransportMessage message, TransportError& error);
        std::optional<TransportEvent> poll(std::chrono::milliseconds timeout);
        std::optional<std::string> peerAddress(TransportConnectionId connection) const;
        void disconnect(TransportConnectionId connection);
        void shutdown(std::chrono::milliseconds timeout);

        bool confirmFingerprint(TransportConnectionId connection,
            std::string_view fingerprint, TransportError& error);
        std::optional<std::string> serverFingerprint() const;
        session::TransitionResult advance(TransportConnectionId connection,
            session::State target, TransportError& error);
        std::optional<session::State> state(TransportConnectionId connection) const;
        session::Endpoint role() const noexcept { return mRole; }

        ITransport& transport() noexcept;

    private:
        Protocol11Endpoint(std::unique_ptr<session::SessionTransport> transport,
            SecureTransport* secureTransport, session::Endpoint role) noexcept;

        std::unique_ptr<session::SessionTransport> mTransport;
        SecureTransport* mSecureTransport = nullptr;
        session::Endpoint mRole;
    };
}

#endif
