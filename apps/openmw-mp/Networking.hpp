#ifndef OPENMW_NETWORKING_HPP
#define OPENMW_NETWORKING_HPP

#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Security/ServerAuthenticationService.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketDispatcher.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketReceiver.hpp>
#include <components/openmw-mp/Transport/Protocol11Endpoint.hpp>
#include "Player.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>

namespace  mwmp
{
    class Networking
    {
    public:
        Networking(RakNet::RakPeerInterface *peer,
            transport::Protocol11Endpoint& endpoint,
            const std::filesystem::path& credentialDirectory,
            const std::filesystem::path& legacyPlayerDirectory,
            unsigned int maximumConnections, unsigned short port);
        ~Networking();

        void newPlayer(RakNet::RakNetGUID guid);
        void disconnectPlayer(RakNet::RakNetGUID guid);
        void kickPlayer(RakNet::RakNetGUID guid, bool sendNotification = true);
        
        void banAddress(const char *ipAddress);
        void unbanAddress(const char *ipAddress);
        std::string getPeerAddress(RakNet::RakNetGUID guid) const;

        void processSystemPacket(RakNet::Packet *packet);
        void processPlayerPacket(RakNet::Packet *packet);
        void processActorPacket(RakNet::Packet *packet);
        void processObjectPacket(RakNet::Packet *packet);
        void processWorldstatePacket(RakNet::Packet *packet);
        void update(RakNet::Packet *packet, RakNet::BitStream &bsIn);

        unsigned short numberOfConnections() const;
        unsigned int maxConnections() const;
        int getAvgPing(RakNet::AddressOrGUID) const;
        unsigned short getPort() const;

        int mainLoop();

        void stopServer(int code);

        SystemPacketController *getSystemPacketController() const;
        PlayerPacketController *getPlayerPacketController() const;
        ActorPacketController *getActorPacketController() const;
        ObjectPacketController *getObjectPacketController() const;
        WorldstatePacketController *getWorldstatePacketController() const;

        BaseActorList *getReceivedActorList();
        BaseObjectList *getReceivedObjectList();
        BaseWorldstate *getReceivedWorldstate();

        int getCurrentMpNum();
        void setCurrentMpNum(int value);
        int incrementMpNum();

        bool getDataFileEnforcementState();
        void setDataFileEnforcementState(bool state);

        bool getScriptErrorIgnoringState();
        void setScriptErrorIgnoringState(bool state);

        bool setServerPassword(std::string_view password, std::string& error);
        bool setServerPasswordHash(std::string passwordHash, std::string& error);
        bool isPassworded() const;

        static const Networking &get();
        static Networking *getPtr();

        void postInit();

        PacketPreInit::PluginContainer &getSamples();
    private:
        bool preInit(RakNet::Packet *packet, RakNet::BitStream &bsIn);
        void processTransportEvent(transport::TransportEvent event);
        void processApplicationMessage(transport::TransportMessage message);
        void processAuthenticationMessage(transport::TransportMessage message);
        bool sendAuthenticationResponse(transport::TransportConnectionId connection,
            const security::AuthenticationResponse& response);
        void disconnectTransport(transport::TransportConnectionId connection,
            const char* reason);
        static Networking *sThis;

        RakNet::RakPeerInterface *peer;
        RakNet::BitStream bsOut;
        TPlayers *players;
        transport::Protocol11Endpoint& mEndpoint;
        transport::ApplicationPacketDispatcher mDispatcher;
        transport::ApplicationPacketReceiver mReceiver;
        security::ServerAuthenticationService mAuthentication;
        std::unordered_set<std::uint64_t> mAuthenticatedConnections;
        std::unordered_set<std::string> mBannedAddresses;
        unsigned int mMaximumConnections;
        unsigned short mPort;

        BaseSystem baseSystem;
        BaseActorList baseActorList;
        BaseObjectList baseObjectList;
        BaseWorldstate baseWorldstate;

        SystemPacketController *systemPacketController;
        PlayerPacketController *playerPacketController;
        ActorPacketController *actorPacketController;
        ObjectPacketController *objectPacketController;
        WorldstatePacketController *worldstatePacketController;

        bool running;
        int exitCode;
        PacketPreInit::PluginContainer samples;
    };
}


#endif //OPENMW_NETWORKING_HPP
