#ifndef OPENMW_NETWORKING_HPP
#define OPENMW_NETWORKING_HPP

#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Mechanics/MovementValidator.hpp>
#include <components/openmw-mp/Mechanics/PlayerLifecycle.hpp>
#include <components/openmw-mp/Mechanics/InventoryLedger.hpp>
#include <components/openmw-mp/Mechanics/CombatResolver.hpp>
#include <components/openmw-mp/Persistence/PersistenceService.hpp>
#include <components/openmw-mp/Security/ServerAuthenticationService.hpp>
#include <components/openmw-mp/Session/AuthorityLease.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketDispatcher.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketReceiver.hpp>
#include <components/openmw-mp/Transport/Protocol11Endpoint.hpp>
#include "Player.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
            unsigned int maximumConnections, unsigned short port,
            double movementMaximumSpeed, unsigned int movementViolationLimit);
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

        std::optional<session::AuthorityLease> assignActorAuthority(
            const ESM::Cell& cell, RakNet::RakNetGUID owner);
        bool validateActorAuthority(const BaseActorList& actorList);
        bool releaseActorAuthority(const ESM::Cell& cell, RakNet::RakNetGUID owner,
            std::uint64_t leaseId);
        bool validatePlayerMovement(Player& player, const BasePlayer& incoming);
        bool authorizePlayerMovement(const Player& player, double tolerance = 128.0);
        void resetPlayerMovement(std::uint64_t connection) noexcept;
        bool acceptPlayerDeath(Player& player);
        bool publishCanonicalPlayerDeath(Player& player, const Target& killer);
        bool beginPlayerRespawn(Player& player, std::uint32_t respawnType);
        bool acknowledgePlayerRespawn(Player& player, const BasePlayer& incoming);
        bool validatePlayerInventory(Player& player, const BasePlayer& incoming);
        bool commitPlayerInventory(Player& player);
        bool applyServerInventoryChanges(Player& player);
        bool validatePlayerStats(Player& player, const BasePlayer& incoming);
        bool reconcilePlayerStats(Player& player);
        bool applyServerPlayerStats(Player& player);
        bool validateActorStats(Player& player, const BaseActorList& incoming);
        bool reconcileActorStats(Player& player, BaseActorList& incoming);
        bool applyServerActorStats(BaseActorList& actorList);
        bool validatePlayerAttack(Player& player, const BasePlayer& incoming);
        void sanitizePlayerAttack(Player& player) noexcept;
        bool resolvePlayerAttack(Player& player, std::string& rejectionReason);
        bool validateActorAttacks(Player& player, const BaseActorList& incoming);
        void sanitizeActorAttack(BaseActor& actor) noexcept;
        bool resolveActorAttack(Player& player, BaseActorList& actorList,
            std::size_t actorIndex, std::optional<BaseActor>& actorDeath,
            std::string& rejectionReason);
        persistence::QueueDecision queuePersistenceWrite(
            std::filesystem::path path, std::string_view contents);
        void flushPersistence();

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
        persistence::PersistenceService mPersistenceService;
        session::AuthorityLeaseManager mAuthorityLeases;
        mechanics::MovementValidator mMovementValidator;
        mechanics::PlayerLifecycle mPlayerLifecycle;
        mechanics::InventoryLedger mInventoryLedger;
        mechanics::CombatResolver mCombatResolver;
        std::unordered_set<std::uint64_t> mAuthenticatedConnections;
        std::unordered_map<std::uint64_t, unsigned int> mAuthorityViolations;
        std::unordered_map<std::uint64_t, unsigned int> mMovementViolations;
        std::unordered_map<std::uint64_t, unsigned int> mLifecycleViolations;
        std::unordered_map<std::uint64_t, unsigned int> mInventoryViolations;
        std::unordered_map<std::uint64_t, unsigned int> mCombatViolations;
        std::unordered_set<std::string> mBannedAddresses;
        unsigned int mMaximumConnections;
        unsigned short mPort;
        double mMovementMaximumSpeed;
        unsigned int mMovementViolationLimit;
        std::uint64_t mCurrentApplicationSequence = 0;

        BaseSystem baseSystem;
        BaseActorList baseActorList;
        BaseObjectList baseObjectList;
        BaseWorldstate baseWorldstate;

        std::unique_ptr<SystemPacketController> systemPacketController;
        std::unique_ptr<PlayerPacketController> playerPacketController;
        std::unique_ptr<ActorPacketController> actorPacketController;
        std::unique_ptr<ObjectPacketController> objectPacketController;
        std::unique_ptr<WorldstatePacketController> worldstatePacketController;

        bool running;
        int exitCode;
        PacketPreInit::PluginContainer samples;
    };
}


#endif //OPENMW_NETWORKING_HPP
