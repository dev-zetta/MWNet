#include "Protocol11Endpoint.hpp"

#include "GameNetworkingSocketsTransport.hpp"
#include "SecureTransport.hpp"

#include <components/openmw-mp/Security/ServerIdentity.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>
#include <components/openmw-mp/Session/SessionTransport.hpp>

#include <utility>

namespace mwmp::transport
{
    Protocol11Endpoint::Protocol11Endpoint(
        std::unique_ptr<session::SessionTransport> transport,
        SecureTransport* secureTransport, session::Endpoint role) noexcept
        : mTransport(std::move(transport))
        , mSecureTransport(secureTransport)
        , mRole(role)
    {
    }

    Protocol11Endpoint::~Protocol11Endpoint() = default;

    std::unique_ptr<Protocol11Endpoint> Protocol11Endpoint::createServer(
        const std::filesystem::path& identityPath, std::string& error)
    {
        auto identity = security::ServerIdentity::loadOrCreate(identityPath, error);
        if (!identity)
            return nullptr;

        auto secure = std::make_unique<SecureTransport>(
            std::make_unique<GameNetworkingSocketsTransport>(), std::move(*identity));
        SecureTransport* securePointer = secure.get();
        auto sessions = std::make_unique<session::SessionTransport>(
            std::move(secure), session::Endpoint::Server);
        return std::unique_ptr<Protocol11Endpoint>(new Protocol11Endpoint(
            std::move(sessions), securePointer, session::Endpoint::Server));
    }

    std::unique_ptr<Protocol11Endpoint> Protocol11Endpoint::createClient(
        const std::filesystem::path& trustStorePath, std::string& error)
    {
        auto trustStore = security::TrustStore::load(trustStorePath, error);
        if (!trustStore)
            return nullptr;

        auto secure = std::make_unique<SecureTransport>(
            std::make_unique<GameNetworkingSocketsTransport>(), std::move(*trustStore));
        SecureTransport* securePointer = secure.get();
        auto sessions = std::make_unique<session::SessionTransport>(
            std::move(secure), session::Endpoint::Client);
        return std::unique_ptr<Protocol11Endpoint>(new Protocol11Endpoint(
            std::move(sessions), securePointer, session::Endpoint::Client));
    }

    bool Protocol11Endpoint::listen(const ListenOptions& options, TransportError& error)
    {
        return mTransport->listen(options, error);
    }

    bool Protocol11Endpoint::connect(const ConnectOptions& options,
        TransportConnectionId& connection, TransportError& error)
    {
        return mTransport->connect(options, connection, error);
    }

    bool Protocol11Endpoint::send(TransportMessage message, TransportError& error)
    {
        return mTransport->send(std::move(message), error);
    }

    std::optional<TransportEvent> Protocol11Endpoint::poll(std::chrono::milliseconds timeout)
    {
        return mTransport->poll(timeout);
    }

    std::optional<std::string> Protocol11Endpoint::peerAddress(
        TransportConnectionId connection) const
    {
        return mTransport->peerAddress(connection);
    }

    void Protocol11Endpoint::disconnect(TransportConnectionId connection)
    {
        mTransport->disconnect(connection);
    }

    void Protocol11Endpoint::shutdown(std::chrono::milliseconds timeout)
    {
        mTransport->shutdown(timeout);
    }

    bool Protocol11Endpoint::confirmFingerprint(TransportConnectionId connection,
        std::string_view fingerprint, TransportError& error)
    {
        if (mRole != session::Endpoint::Client)
        {
            error = { TransportErrorCode::InvalidConfiguration,
                "only clients can confirm a server fingerprint" };
            return false;
        }
        return mSecureTransport->confirmFingerprint(connection, fingerprint, error);
    }

    std::optional<std::string> Protocol11Endpoint::serverFingerprint() const
    {
        return mSecureTransport->serverFingerprint();
    }

    session::TransitionResult Protocol11Endpoint::advance(TransportConnectionId connection,
        session::State target, TransportError& error)
    {
        return mTransport->advance(connection, target, error);
    }

    std::optional<session::State> Protocol11Endpoint::state(
        TransportConnectionId connection) const
    {
        return mTransport->state(connection);
    }

    ITransport& Protocol11Endpoint::transport() noexcept
    {
        return *mTransport;
    }
}
