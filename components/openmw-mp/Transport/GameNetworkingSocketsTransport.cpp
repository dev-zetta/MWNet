#include "GameNetworkingSocketsTransport.hpp"

#include "TransportCodec.hpp"
#include "TransportQueue.hpp"

#include <components/openmw-mp/Protocol/EndpointSecurity.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>
#include <components/openmw-mp/Protocol/RateLimits.hpp>

#include <steam/steamnetworkingsockets.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
#include <steam/steamnetworkingsockets_flat.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <steam/isteamnetworkingutils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mwmp::transport
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr std::size_t sMaximumQueuedMessages = 4096;
        constexpr std::size_t sMaximumQueuedBytes = 64U * 1024U * 1024U;
        constexpr std::size_t sMaximumViolations = 3;
        constexpr std::chrono::milliseconds sWorkerInterval{ 2 };

        TransportConnectionId connectionId(HSteamNetConnection handle)
        {
            return TransportConnectionId{ static_cast<std::uint64_t>(handle) };
        }

        HSteamNetConnection connectionHandle(TransportConnectionId id)
        {
            if (!id || id.value > std::numeric_limits<HSteamNetConnection>::max())
                return k_HSteamNetConnection_Invalid;
            return static_cast<HSteamNetConnection>(id.value);
        }

        bool parseAddress(const std::string& text, std::uint16_t port, SteamNetworkingIPAddr& address)
        {
            if (!address.ParseString(text.c_str()))
                return false;
            address.m_port = port;
            return true;
        }

        DeliveryMode deliveryMode(int flags)
        {
            return (flags & k_nSteamNetworkingSend_Reliable) != 0 ? DeliveryMode::ReliableOrdered
                                                                  : DeliveryMode::Unreliable;
        }

        int sendFlags(DeliveryMode delivery)
        {
            return delivery == DeliveryMode::ReliableOrdered ? k_nSteamNetworkingSend_Reliable
                                                              : k_nSteamNetworkingSend_UnreliableNoNagle;
        }
    }

    struct GameNetworkingSocketsTransport::Impl
    {
        struct Connection
        {
            explicit Connection(TransportTimeouts value, Clock::time_point now)
                : timeouts(value)
                , connectStarted(now)
                , lastReceived(now)
                , limiter(now)
            {
            }

            TransportTimeouts timeouts;
            Clock::time_point connectStarted;
            Clock::time_point lastReceived;
            protocol::ConnectionRateLimiter limiter;
            std::size_t violations = 0;
            bool connectedEventSent = false;
        };

        struct PendingMessage
        {
            TransportMessage message;
            std::size_t encodedBytes = 0;
        };

        static std::mutex sLibraryMutex;
        static std::size_t sLibraryUsers;
        static std::mutex sCallbackMutex;
        static std::mutex sRegistryMutex;
        static std::unordered_map<HSteamNetConnection, Impl*> sConnectionOwners;
        static std::unordered_map<HSteamListenSocket, Impl*> sListenOwners;

        ISteamNetworkingSockets* interface = nullptr;
        HSteamListenSocket listenSocket = k_HSteamListenSocket_Invalid;
        HSteamNetPollGroup pollGroup = k_HSteamNetPollGroup_Invalid;
        std::optional<std::uint16_t> listeningPort;
        std::size_t maximumConnections = 0;
        TransportTimeouts listenTimeouts;

        mutable std::mutex stateMutex;
        std::unordered_map<HSteamNetConnection, Connection> connections;
        TransportQueue incoming;

        std::mutex outgoingMutex;
        std::condition_variable_any outgoingChanged;
        std::deque<PendingMessage> outgoing;
        std::size_t outgoingBytes = 0;

        std::jthread worker;
        bool initialized = false;
        bool stopping = false;

        ~Impl()
        {
            shutdown(std::chrono::milliseconds(0));
            releaseLibrary();
        }

        static void statusCallback(SteamNetConnectionStatusChangedCallback_t* info)
        {
            Impl* owner = nullptr;
            {
                std::scoped_lock lock(sRegistryMutex);
                if (const auto found = sConnectionOwners.find(info->m_hConn);
                    found != sConnectionOwners.end())
                    owner = found->second;
                else if (const auto listener = sListenOwners.find(info->m_info.m_hListenSocket);
                         listener != sListenOwners.end())
                    owner = listener->second;
            }
            if (owner != nullptr)
                owner->onStatusChanged(*info);
        }

        bool initialize(TransportError& error)
        {
            if (initialized)
                return true;

            {
                std::scoped_lock lock(sLibraryMutex);
                if (sLibraryUsers == 0)
                {
                    SteamDatagramErrMsg message{};
                    if (!GameNetworkingSockets_Init(nullptr, message))
                    {
                        error = { TransportErrorCode::Internal, message };
                        return false;
                    }
                }
                ++sLibraryUsers;
            }

            interface = SteamNetworkingSockets();
            if (interface == nullptr)
            {
                error = { TransportErrorCode::Internal,
                    "failed to obtain the GameNetworkingSockets interface" };
                std::scoped_lock lock(sLibraryMutex);
                if (sLibraryUsers > 0 && --sLibraryUsers == 0)
                    GameNetworkingSockets_Kill();
                return false;
            }

            pollGroup = SteamAPI_ISteamNetworkingSockets_CreatePollGroup(interface);
            if (pollGroup == k_HSteamNetPollGroup_Invalid)
            {
                error = { TransportErrorCode::Internal, "failed to create a transport poll group" };
                interface = nullptr;
                std::scoped_lock lock(sLibraryMutex);
                if (sLibraryUsers > 0 && --sLibraryUsers == 0)
                    GameNetworkingSockets_Kill();
                return false;
            }

            initialized = true;
            worker = std::jthread([this](std::stop_token stopToken) { workerLoop(stopToken); });
            return true;
        }

        void releaseLibrary()
        {
            if (!initialized)
                return;
            initialized = false;
            interface = nullptr;
            std::scoped_lock lock(sLibraryMutex);
            if (sLibraryUsers > 0 && --sLibraryUsers == 0)
                GameNetworkingSockets_Kill();
        }

        std::array<SteamNetworkingConfigValue_t, 4> connectionOptions()
        {
            std::array<SteamNetworkingConfigValue_t, 4> options;
            options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                reinterpret_cast<void*>(statusCallback));
            options[1].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize,
                static_cast<std::int32_t>(protocol::limits::normalMessageBytes + protocol::envelopeBytes));
            options[2].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages,
                static_cast<std::int32_t>(sMaximumQueuedMessages));
            // Open-source IP connections do not have a certificate authority. TES3MP's
            // identity-bound TOFU handshake authenticates them before gameplay.
            options[3].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 2);
            return options;
        }

        bool listen(const ListenOptions& options, TransportError& error)
        {
            if (!protocol::isListenAddressAllowed(options.address, options.publicListen))
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "non-loopback listening requires publicListen=true" };
                return false;
            }
            if (options.maximumConnections == 0)
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "maximumConnections must be greater than zero" };
                return false;
            }
            if (options.port == 0)
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "GameNetworkingSockets requires a nonzero listen port" };
                return false;
            }
            if (!initialize(error))
                return false;

            SteamNetworkingIPAddr address;
            if (!parseAddress(options.address, options.port, address))
            {
                error = { TransportErrorCode::InvalidConfiguration, "invalid numeric listen address" };
                return false;
            }

            std::scoped_lock callbackLock(sCallbackMutex);
            std::scoped_lock lock(stateMutex);
            if (stopping || listenSocket != k_HSteamListenSocket_Invalid)
            {
                error = { TransportErrorCode::InvalidConfiguration, "transport is already listening" };
                return false;
            }

            auto settings = connectionOptions();
            listenSocket = SteamAPI_ISteamNetworkingSockets_CreateListenSocketIP(
                interface, address, static_cast<int>(settings.size()), settings.data());
            if (listenSocket == k_HSteamListenSocket_Invalid)
            {
                error = { TransportErrorCode::ConnectionFailed, "failed to create the listen socket" };
                return false;
            }

            SteamNetworkingIPAddr boundAddress;
            if (!SteamAPI_ISteamNetworkingSockets_GetListenSocketAddress(
                    interface, listenSocket, &boundAddress))
            {
                SteamAPI_ISteamNetworkingSockets_CloseListenSocket(interface, listenSocket);
                listenSocket = k_HSteamListenSocket_Invalid;
                error = { TransportErrorCode::Internal, "failed to query the bound listen address" };
                return false;
            }

            maximumConnections = options.maximumConnections;
            listenTimeouts = options.timeouts;
            listeningPort = boundAddress.m_port;
            {
                std::scoped_lock registryLock(sRegistryMutex);
                sListenOwners.emplace(listenSocket, this);
            }
            return true;
        }

        bool connect(const ConnectOptions& options, TransportConnectionId& result, TransportError& error)
        {
            if (options.host.empty())
            {
                error = { TransportErrorCode::InvalidConfiguration, "connection host must not be empty" };
                return false;
            }
            if (!initialize(error))
                return false;

            SteamNetworkingIPAddr address;
            if (!parseAddress(options.host, options.port, address))
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "GameNetworkingSockets transport currently requires a numeric IP address" };
                return false;
            }

            auto settings = connectionOptions();
            std::scoped_lock callbackLock(sCallbackMutex);
            const HSteamNetConnection handle = SteamAPI_ISteamNetworkingSockets_ConnectByIPAddress(
                interface, address, static_cast<int>(settings.size()), settings.data());
            if (handle == k_HSteamNetConnection_Invalid)
            {
                error = { TransportErrorCode::ConnectionFailed, "failed to create the outbound connection" };
                return false;
            }

            const auto now = Clock::now();
            {
                std::scoped_lock lock(stateMutex);
                if (stopping)
                {
                    SteamAPI_ISteamNetworkingSockets_CloseConnection(
                        interface, handle, 0, "transport stopping", false);
                    error = { TransportErrorCode::Closed, "transport is stopping" };
                    return false;
                }
                connections.try_emplace(handle, options.timeouts, now);
            }
            {
                std::scoped_lock registryLock(sRegistryMutex);
                sConnectionOwners.emplace(handle, this);
            }
            SteamAPI_ISteamNetworkingSockets_SetConnectionPollGroup(interface, handle, pollGroup);
            result = connectionId(handle);
            return true;
        }

        bool enqueue(TransportMessage message, TransportError& error)
        {
            if (message.delivery == DeliveryMode::ReliableUnordered)
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "GameNetworkingSockets guarantees reliable ordering only within a lane" };
                return false;
            }
            if (static_cast<std::uint8_t>(message.lane)
                > static_cast<std::uint8_t>(MessageLane::Worldstate))
            {
                error = { TransportErrorCode::InvalidConfiguration, "invalid transport lane" };
                return false;
            }

            std::vector<std::byte> encoded;
            protocol::CodecError codecError = protocol::CodecError::None;
            if (!encodeTransportMessage(message, encoded, codecError))
            {
                error = { TransportErrorCode::MessageRejected, "protocol-11 message encoding failed" };
                return false;
            }

            const HSteamNetConnection handle = connectionHandle(message.connection);
            {
                std::scoped_lock lock(stateMutex);
                if (stopping || connections.find(handle) == connections.end())
                {
                    error = { TransportErrorCode::Closed, "connection is not active" };
                    return false;
                }
            }

            std::unique_lock lock(outgoingMutex);
            if (outgoing.size() >= sMaximumQueuedMessages
                || encoded.size() > sMaximumQueuedBytes - outgoingBytes)
            {
                error = { TransportErrorCode::QueueFull, "outbound transport queue is full" };
                return false;
            }
            outgoingBytes += encoded.size();
            outgoing.push_back({ std::move(message), encoded.size() });
            lock.unlock();
            outgoingChanged.notify_one();
            return true;
        }

        void workerLoop(std::stop_token stopToken)
        {
            while (!stopToken.stop_requested())
            {
                {
                    std::scoped_lock callbackLock(sCallbackMutex);
                    SteamAPI_ISteamNetworkingSockets_RunCallbacks(interface);
                }
                drainOutgoing();
                receiveMessages();
                enforceTimeouts();

                std::unique_lock lock(outgoingMutex);
                outgoingChanged.wait_for(lock, stopToken, sWorkerInterval,
                    [this] { return !outgoing.empty(); });
            }
        }

        void drainOutgoing()
        {
            for (std::size_t count = 0; count < 128; ++count)
            {
                PendingMessage pending;
                {
                    std::scoped_lock lock(outgoingMutex);
                    if (outgoing.empty())
                        return;
                    pending = std::move(outgoing.front());
                    outgoing.pop_front();
                    outgoingBytes -= pending.encodedBytes;
                }

                std::vector<std::byte> encoded;
                protocol::CodecError error = protocol::CodecError::None;
                if (!encodeTransportMessage(pending.message, encoded, error))
                {
                    recordViolation(connectionHandle(pending.message.connection), "message encoding failed");
                    continue;
                }

                SteamNetworkingMessage_t* networkMessage = SteamAPI_ISteamNetworkingUtils_AllocateMessage(
                    SteamNetworkingUtils(), static_cast<int>(encoded.size()));
                if (networkMessage == nullptr)
                {
                    recordViolation(connectionHandle(pending.message.connection), "message allocation failed");
                    continue;
                }
                std::memcpy(networkMessage->m_pData, encoded.data(), encoded.size());
                networkMessage->m_cbSize = static_cast<int>(encoded.size());
                networkMessage->m_conn = connectionHandle(pending.message.connection);
                networkMessage->m_nFlags = sendFlags(pending.message.delivery);
                networkMessage->m_idxLane = static_cast<std::uint16_t>(pending.message.lane);

                int64 sendResult = 0;
                SteamAPI_ISteamNetworkingSockets_SendMessages(
                    interface, 1, &networkMessage, &sendResult, true);
                if (sendResult < 0)
                    recordViolation(connectionHandle(pending.message.connection), "network send rejected");
            }
        }

        void receiveMessages()
        {
            for (std::size_t count = 0; count < 256; ++count)
            {
                SteamNetworkingMessage_t* networkMessage = nullptr;
                const int received = SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnPollGroup(
                    interface, pollGroup, &networkMessage, 1);
                if (received <= 0)
                    return;

                const HSteamNetConnection handle = networkMessage->m_conn;
                processReceived(*networkMessage);
                networkMessage->Release();

                std::scoped_lock lock(stateMutex);
                if (const auto found = connections.find(handle); found != connections.end())
                    found->second.lastReceived = Clock::now();
            }
        }

        void processReceived(const SteamNetworkingMessage_t& networkMessage)
        {
            if (networkMessage.m_idxLane > static_cast<std::uint16_t>(MessageLane::Worldstate)
                || networkMessage.m_cbSize < 0)
            {
                recordViolation(networkMessage.m_conn, "invalid message lane or size");
                return;
            }

            bool rejectedByRateLimit = false;
            bool disconnectForRateLimit = false;
            {
                std::scoped_lock lock(stateMutex);
                const auto found = connections.find(networkMessage.m_conn);
                if (found == connections.end())
                    return;
                if (!found->second.limiter.consume(static_cast<std::size_t>(networkMessage.m_cbSize)))
                {
                    rejectedByRateLimit = true;
                    disconnectForRateLimit = ++found->second.violations >= sMaximumViolations;
                }
            }
            if (disconnectForRateLimit)
                closeConnection(networkMessage.m_conn, "transport rate limit exceeded", false);
            if (rejectedByRateLimit)
                return;

            const auto raw = std::span(static_cast<const std::byte*>(networkMessage.m_pData),
                static_cast<std::size_t>(networkMessage.m_cbSize));
            TransportMessage message;
            const auto decoded = decodeTransportMessage(raw, connectionId(networkMessage.m_conn),
                static_cast<MessageLane>(networkMessage.m_idxLane), deliveryMode(networkMessage.m_nFlags), message);
            if (!decoded)
            {
                recordViolation(networkMessage.m_conn, "invalid protocol-11 message");
                return;
            }

            if (!incoming.tryPush(
                    { TransportEventType::Message, message.connection, std::move(message), {} }))
                closeConnection(networkMessage.m_conn, "inbound transport queue is full", false);
        }

        void onStatusChanged(const SteamNetConnectionStatusChangedCallback_t& info)
        {
            switch (info.m_info.m_eState)
            {
                case k_ESteamNetworkingConnectionState_Connecting:
                    acceptIncoming(info);
                    break;
                case k_ESteamNetworkingConnectionState_Connected:
                    markConnected(info.m_hConn);
                    break;
                case k_ESteamNetworkingConnectionState_ClosedByPeer:
                case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                    markDisconnected(info.m_hConn, info.m_info.m_szEndDebug);
                    break;
                default:
                    break;
            }
        }

        void acceptIncoming(const SteamNetConnectionStatusChangedCallback_t& info)
        {
            if (listenSocket == k_HSteamListenSocket_Invalid
                || info.m_info.m_hListenSocket != listenSocket)
                return;

            {
                std::scoped_lock lock(stateMutex);
                if (connections.find(info.m_hConn) != connections.end())
                    return;
            }

            {
                std::scoped_lock lock(stateMutex);
                if (stopping || connections.size() >= maximumConnections)
                {
                    SteamAPI_ISteamNetworkingSockets_CloseConnection(
                        interface, info.m_hConn, 0, "server capacity reached", false);
                    return;
                }
            }

            if (SteamAPI_ISteamNetworkingSockets_AcceptConnection(interface, info.m_hConn)
                    != k_EResultOK
                || !SteamAPI_ISteamNetworkingSockets_SetConnectionPollGroup(
                    interface, info.m_hConn, pollGroup))
            {
                SteamAPI_ISteamNetworkingSockets_CloseConnection(
                    interface, info.m_hConn, 0, "failed to accept connection", false);
                return;
            }

            const auto now = Clock::now();
            {
                std::scoped_lock lock(stateMutex);
                connections.try_emplace(info.m_hConn, listenTimeouts, now);
            }
            {
                std::scoped_lock registryLock(sRegistryMutex);
                sConnectionOwners.emplace(info.m_hConn, this);
            }
        }

        void markConnected(HSteamNetConnection handle)
        {
            constexpr std::array<int, 5> priorities{ 0, 0, 0, 0, 0 };
            constexpr std::array<std::uint16_t, 5> weights{ 8, 4, 4, 4, 4 };
            if (SteamAPI_ISteamNetworkingSockets_ConfigureConnectionLanes(interface, handle,
                    static_cast<int>(priorities.size()), priorities.data(), weights.data())
                != k_EResultOK)
            {
                closeConnection(handle, "failed to configure message lanes", false);
                return;
            }

            {
                std::scoped_lock lock(stateMutex);
                const auto found = connections.find(handle);
                if (found == connections.end() || found->second.connectedEventSent)
                    return;
                found->second.connectedEventSent = true;
                found->second.lastReceived = Clock::now();
            }

            const TransportConnectionId id = connectionId(handle);
            if (!incoming.tryPush({ TransportEventType::Connected, id, {}, {} }))
                closeConnection(handle, "inbound transport queue is full", false);
        }

        void markDisconnected(HSteamNetConnection handle, const char* detail)
        {
            bool existed = false;
            {
                std::scoped_lock lock(stateMutex);
                existed = connections.erase(handle) != 0;
            }
            {
                std::scoped_lock registryLock(sRegistryMutex);
                sConnectionOwners.erase(handle);
            }
            SteamAPI_ISteamNetworkingSockets_CloseConnection(interface, handle, 0, nullptr, false);

            if (existed)
            {
                std::string description = detail == nullptr ? std::string{} : std::string(detail);
                description.resize(std::min<std::size_t>(description.size(), 256));
                incoming.tryPush({ TransportEventType::Disconnected, connectionId(handle), {},
                    std::move(description) });
            }
        }

        void recordViolation(HSteamNetConnection handle, const char* reason)
        {
            bool shouldClose = false;
            {
                std::scoped_lock lock(stateMutex);
                if (const auto found = connections.find(handle); found != connections.end())
                    shouldClose = ++found->second.violations >= sMaximumViolations;
            }
            if (shouldClose)
                closeConnection(handle, reason, false);
        }

        void enforceTimeouts()
        {
            const auto now = Clock::now();
            std::vector<std::pair<HSteamNetConnection, const char*>> expired;
            {
                std::scoped_lock lock(stateMutex);
                for (const auto& [handle, connection] : connections)
                {
                    if (!connection.connectedEventSent
                        && now - connection.connectStarted > connection.timeouts.connect)
                        expired.emplace_back(handle, "connection deadline exceeded");
                    else if (connection.connectedEventSent
                        && now - connection.lastReceived > connection.timeouts.read)
                        expired.emplace_back(handle, "read deadline exceeded");
                }
            }
            for (const auto& [handle, reason] : expired)
                closeConnection(handle, reason, false);
        }

        void closeConnection(HSteamNetConnection handle, const char* reason, bool linger)
        {
            if (handle != k_HSteamNetConnection_Invalid && interface != nullptr)
                SteamAPI_ISteamNetworkingSockets_CloseConnection(interface, handle, 0, reason, linger);
        }

        void disconnect(TransportConnectionId id)
        {
            closeConnection(connectionHandle(id), "application disconnect", true);
        }

        std::optional<std::string> peerAddress(TransportConnectionId id) const
        {
            const HSteamNetConnection handle = connectionHandle(id);
            std::scoped_lock callbackLock(sCallbackMutex);
            std::scoped_lock lock(stateMutex);
            if (interface == nullptr || connections.find(handle) == connections.end())
                return std::nullopt;
            SteamNetConnectionInfo_t information;
            if (!SteamAPI_ISteamNetworkingSockets_GetConnectionInfo(interface, handle, &information))
                return std::nullopt;
            char address[SteamNetworkingIPAddr::k_cchMaxString]{};
            information.m_addrRemote.ToString(address, sizeof(address), false);
            return std::string(address);
        }

        void shutdown(std::chrono::milliseconds timeout)
        {
            (void)timeout;
            {
                std::scoped_lock lock(stateMutex);
                if (stopping)
                    return;
                stopping = true;
            }

            if (worker.joinable())
            {
                worker.request_stop();
                outgoingChanged.notify_all();
                worker.join();
            }

            if (interface != nullptr)
            {
                std::vector<HSteamNetConnection> handles;
                {
                    std::scoped_lock lock(stateMutex);
                    handles.reserve(connections.size());
                    for (const auto& [handle, connection] : connections)
                    {
                        (void)connection;
                        handles.push_back(handle);
                    }
                    connections.clear();
                }
                for (const HSteamNetConnection handle : handles)
                    SteamAPI_ISteamNetworkingSockets_CloseConnection(
                        interface, handle, 0, "transport shutdown", false);

                {
                    std::scoped_lock registryLock(sRegistryMutex);
                    for (const HSteamNetConnection handle : handles)
                        sConnectionOwners.erase(handle);
                    if (listenSocket != k_HSteamListenSocket_Invalid)
                        sListenOwners.erase(listenSocket);
                }

                if (listenSocket != k_HSteamListenSocket_Invalid)
                {
                    SteamAPI_ISteamNetworkingSockets_CloseListenSocket(interface, listenSocket);
                    listenSocket = k_HSteamListenSocket_Invalid;
                }
                if (pollGroup != k_HSteamNetPollGroup_Invalid)
                {
                    SteamAPI_ISteamNetworkingSockets_DestroyPollGroup(interface, pollGroup);
                    pollGroup = k_HSteamNetPollGroup_Invalid;
                }
            }
            incoming.close();
        }
    };

    std::mutex GameNetworkingSocketsTransport::Impl::sLibraryMutex;
    std::size_t GameNetworkingSocketsTransport::Impl::sLibraryUsers = 0;
    std::mutex GameNetworkingSocketsTransport::Impl::sCallbackMutex;
    std::mutex GameNetworkingSocketsTransport::Impl::sRegistryMutex;
    std::unordered_map<HSteamNetConnection, GameNetworkingSocketsTransport::Impl*>
        GameNetworkingSocketsTransport::Impl::sConnectionOwners;
    std::unordered_map<HSteamListenSocket, GameNetworkingSocketsTransport::Impl*>
        GameNetworkingSocketsTransport::Impl::sListenOwners;

    GameNetworkingSocketsTransport::GameNetworkingSocketsTransport()
        : mImpl(std::make_unique<Impl>())
    {
    }

    GameNetworkingSocketsTransport::~GameNetworkingSocketsTransport() = default;

    bool GameNetworkingSocketsTransport::listen(const ListenOptions& options, TransportError& error)
    {
        error = {};
        return mImpl->listen(options, error);
    }

    bool GameNetworkingSocketsTransport::connect(
        const ConnectOptions& options, TransportConnectionId& connection, TransportError& error)
    {
        error = {};
        return mImpl->connect(options, connection, error);
    }

    bool GameNetworkingSocketsTransport::send(TransportMessage message, TransportError& error)
    {
        error = {};
        return mImpl->enqueue(std::move(message), error);
    }

    std::optional<TransportEvent> GameNetworkingSocketsTransport::poll(std::chrono::milliseconds timeout)
    {
        return mImpl->incoming.waitPop(timeout);
    }

    std::optional<std::string> GameNetworkingSocketsTransport::peerAddress(
        TransportConnectionId connection) const
    {
        return mImpl->peerAddress(connection);
    }

    void GameNetworkingSocketsTransport::disconnect(TransportConnectionId connection)
    {
        mImpl->disconnect(connection);
    }

    void GameNetworkingSocketsTransport::shutdown(std::chrono::milliseconds timeout)
    {
        mImpl->shutdown(timeout);
    }

    std::optional<std::uint16_t> GameNetworkingSocketsTransport::boundPort() const
    {
        std::scoped_lock lock(mImpl->stateMutex);
        return mImpl->listeningPort;
    }
}
