#ifndef OPENMW_MASTERQUERY_HPP
#define OPENMW_MASTERQUERY_HPP

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <RakPeerInterface.h>
#include <components/openmw-mp/Master/MasterData.hpp>
#include <components/openmw-mp/Master/PacketMasterQuery.hpp>
#include <components/openmw-mp/Master/PacketMasterUpdate.hpp>

namespace mwmp
{
    // Qt-free mirror of apps/browser/netutils/QueryClient, for use in-game.
    class MasterQuery
    {
    public:
        MasterQuery();
        ~MasterQuery();

        void setServer(const std::string& addr, unsigned short port);

        // Returns map of address -> QueryData for all servers on the master.
        // Blocking – run from a background thread.
        std::map<RakNet::SystemAddress, QueryData> query();

        // Fetch updated details for a single server (player list etc.).
        std::pair<RakNet::SystemAddress, QueryData> update(const RakNet::SystemAddress& addr);

        // Ping all servers in parallel. Returns map of "host:port" -> ms (-1 = no response).
        static std::map<std::string, int> pingServers(
            const std::vector<std::pair<std::string, unsigned short>>& targets);

        int status() const { return mStatus; }

    private:
        RakNet::ConnectionState connect();
        int waitForPacket(int waitingPacket,
                          std::map<RakNet::SystemAddress, QueryData>* servers,
                          std::pair<RakNet::SystemAddress, QueryData>* single);

        RakNet::RakPeerInterface* mPeer;
        RakNet::SystemAddress     mMasterAddr;
        PacketMasterQuery*        mPmq;
        PacketMasterUpdate*       mPmu;
        int                       mStatus;
    };
}

#endif // OPENMW_MASTERQUERY_HPP
