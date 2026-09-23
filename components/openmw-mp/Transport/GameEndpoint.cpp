#include "GameEndpoint.hpp"

#include "GameNetworkingSocketsTransport.hpp"
#include "SecureTransport.hpp"

#include <components/openmw-mp/Security/ServerIdentity.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>
#include <components/openmw-mp/Session/SessionTransport.hpp>

#include <utility>

namespace mwmp::transport
{
    GameEndpoint::GameEndpoint(
        std::unique_ptr<session::SessionTransport> transport,
        SecureTransport* secureTransport, session::Endpoint role) noexcept
        : mTransport(std::move(transport))
        , mSecureTransport(secureTransport)
        , mRole(role)
    {
    }

    GameEndpoint::~GameEndpoint() = default;

    std::unique_ptr<GameEndpoint> GameEndpoint::createServer(
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
        return std::unique_ptr<GameEndpoint>(new GameEndpoint(
            std::move(sessions), securePointer, session::Endpoint::Server));
    }

    std::unique_ptr<GameEndpoint> GameEndpoint::createClient(
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
        return std::unique_ptr<GameEndpoint>(new GameEndpoint(
            std::move(sessions), securePointer, session::Endpoint::Client));
    }

    std::unique_ptr<GameEndpoint> GameEndpoint::createProbe()
    {
        auto secure = std::make_unique<SecureTransport>(
            std::make_unique<GameNetworkingSocketsTransport>(), security::TrustStore::ephemeral());
        auto* pointer = secure.get();
        auto sessions = std::make_unique<session::SessionTransport>(
            std::move(secure), session::Endpoint::Client);
        return std::unique_ptr<GameEndpoint>(new GameEndpoint(
            std::move(sessions), pointer, session::Endpoint::Client));
    }

    bool GameEndpoint::listen(const ListenOptions& options, TransportError& error)
    {
        return mTransport->listen(options, error);
    }

    bool GameEndpoint::connect(const ConnectOptions& options,
        TransportConnectionId& connection, TransportError& error)
    {
        return mTransport->connect(options, connection, error);
    }

    bool GameEndpoint::send(TransportMessage message, TransportError& error)
    {
        return mTransport->send(std::move(message), error);
    }

    std::optional<TransportEvent> GameEndpoint::poll(std::chrono::milliseconds timeout)
    {
        return mTransport->poll(timeout);
    }

    std::optional<std::string> GameEndpoint::peerAddress(
        TransportConnectionId connection) const
    {
        return mTransport->peerAddress(connection);
    }

    void GameEndpoint::disconnect(TransportConnectionId connection)
    {
        mTransport->disconnect(connection);
    }

    void GameEndpoint::shutdown(std::chrono::milliseconds timeout)
    {
        mTransport->shutdown(timeout);
    }

    bool GameEndpoint::confirmFingerprint(TransportConnectionId connection,
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

    std::optional<std::string> GameEndpoint::serverFingerprint() const
    {
        return mSecureTransport->serverFingerprint();
    }

    session::TransitionResult GameEndpoint::advance(TransportConnectionId connection,
        session::State target, TransportError& error)
    {
        return mTransport->advance(connection, target, error);
    }

    std::optional<session::State> GameEndpoint::state(
        TransportConnectionId connection) const
    {
        return mTransport->state(connection);
    }

    ITransport& GameEndpoint::transport() noexcept
    {
        return *mTransport;
    }
}
