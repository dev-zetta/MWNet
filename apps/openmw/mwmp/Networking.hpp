#ifndef OPENMW_NETWORKING_HPP
#define OPENMW_NETWORKING_HPP

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketDispatcher.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketReceiver.hpp>
#include <components/openmw-mp/Transport/Protocol11Endpoint.hpp>

#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>

#include <components/files/collections.hpp>

#include "LocalSystem.hpp"
#include "ActorList.hpp"
#include "ObjectList.hpp"
#include "Worldstate.hpp"

namespace mwmp
{
    class LocalPlayer;

    struct ClientConnectionOptions
    {
        ClientConnectionOptions() = default;
        ClientConnectionOptions(ClientConnectionOptions&&) noexcept = default;
        ClientConnectionOptions& operator=(ClientConnectionOptions&&) noexcept = default;
        ClientConnectionOptions(const ClientConnectionOptions&) = delete;
        ClientConnectionOptions& operator=(const ClientConnectionOptions&) = delete;
        ~ClientConnectionOptions();

        std::string accountName;
        std::string accountPassword;
        std::string serverAccessPassword;
        std::optional<std::string> trustedFingerprint;
        bool registerAccount = false;
    };

    class Networking
    {
    public:
        Networking();
        ~Networking();
        void connect(const std::string& ip, unsigned short port,
            std::vector<std::string>& content, Files::Collections& collections,
            ClientConnectionOptions options);
        void update();

        SystemPacket *getSystemPacket(std::uint16_t id);
        PlayerPacket *getPlayerPacket(std::uint16_t id);
        ActorPacket *getActorPacket(std::uint16_t id);
        ObjectPacket *getObjectPacket(std::uint16_t id);
        WorldstatePacket *getWorldstatePacket(std::uint16_t id);

        bool isConnected();
        void disconnect();
        void setLastError(const std::string& msg) { lastError = msg; }
        const std::string& getLastError() const { return lastError; }

        LocalSystem *getLocalSystem();
        LocalPlayer *getLocalPlayer();
        ActorList *getActorList();
        ObjectList *getObjectList();
        Worldstate *getWorldstate();

    private:
        bool connected;
        std::string lastError;
        std::deque<transport::ReceivedApplicationPacket> pendingPackets;
        std::size_t pendingPacketBytes = 0;
        std::unique_ptr<transport::Protocol11Endpoint> endpoint;
        std::unique_ptr<transport::ApplicationPacketDispatcher> dispatcher;
        transport::ApplicationPacketReceiver receiver;
        transport::TransportConnectionId serverConnection;

        SystemPacketController systemPacketController;
        PlayerPacketController playerPacketController;
        ActorPacketController actorPacketController;
        ObjectPacketController objectPacketController;
        WorldstatePacketController worldstatePacketController;

        ActorList actorList;
        ObjectList objectList;
        Worldstate worldstate;

        void receiveMessage(const transport::ReceivedApplicationPacket& packet);
        void processTransportEvent(transport::TransportEvent event);
        bool preInit(std::vector<std::string>& content, Files::Collections& collections);
        bool authenticate(ClientConnectionOptions& options);
        bool requestSpawn();
        bool confirmServerFingerprint(std::string_view host, unsigned short port,
            std::string_view fingerprint);
        bool failConnection(std::string message);
        bool receiveApplicationMessage(const transport::TransportMessage& message,
            transport::ReceivedApplicationPacket& packet);
    };
}


#endif //OPENMW_NETWORKING_HPP
