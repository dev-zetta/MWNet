#include "MasterQuery.hpp"

#include <GetTime.h>
#include <RakSleep.h>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/TimedLog.hpp>

using namespace RakNet;

namespace mwmp
{

MasterQuery::MasterQuery()
    : mStatus(-1)
{
    mPeer = RakPeerInterface::GetInstance();
    mPmq  = new PacketMasterQuery(mPeer);
    mPmu  = new PacketMasterUpdate(mPeer);

    SocketDescriptor sd;
    mPeer->Startup(8, &sd, 1);
}

MasterQuery::~MasterQuery()
{
    delete mPmq;
    delete mPmu;
    RakPeerInterface::DestroyInstance(mPeer);
}

void MasterQuery::setServer(const std::string& addr, unsigned short port)
{
    mMasterAddr = SystemAddress(addr.c_str(), port);
}

std::map<SystemAddress, QueryData> MasterQuery::query()
{
    std::map<SystemAddress, QueryData> result;
    BitStream bs;
    bs.Write((unsigned char)ID_MASTER_QUERY);

    mStatus = -1;
    int attempts = 3;
    do
    {
        if (connect() == IS_NOT_CONNECTED)
            return result;

        if (mPeer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_MASTER, mMasterAddr, false) == 0)
            return result;

        mPmq->SetServers(&result);
        mStatus = waitForPacket(ID_MASTER_QUERY, &result, nullptr);
        RakSleep(100);
    }
    while (mStatus != ID_MASTER_QUERY && --attempts > 0);

    mPeer->CloseConnection(mMasterAddr, true);
    return result;
}

std::pair<SystemAddress, QueryData> MasterQuery::update(const SystemAddress& addr)
{
    std::pair<SystemAddress, QueryData> server;
    BitStream bs;
    bs.Write((unsigned char)ID_MASTER_UPDATE);
    bs.Write(addr);

    mStatus = -1;
    int attempts = 3;
    mPmu->SetServer(&server);
    do
    {
        if (connect() == IS_NOT_CONNECTED)
            return server;

        mPeer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_MASTER, mMasterAddr, false);
        mStatus = waitForPacket(ID_MASTER_UPDATE, nullptr, &server);
        RakSleep(100);
    }
    while (mStatus != ID_MASTER_UPDATE && --attempts > 0);

    mPeer->CloseConnection(mMasterAddr, true);
    return server;
}

ConnectionState MasterQuery::connect()
{
    mPeer->Connect(mMasterAddr.ToString(false), mMasterAddr.GetPort(),
                   TES3MP_MASTERSERVER_PASSW, strlen(TES3MP_MASTERSERVER_PASSW),
                   nullptr, 0, 5, 500);

    while (true)
    {
        // Pump receive to allow RakNet to process the connection handshake
        for (Packet* p = mPeer->Receive(); p; mPeer->DeallocatePacket(p), p = mPeer->Receive())
        {
            unsigned char pid = p->data[0];
            if (pid == ID_CONNECTION_REQUEST_ACCEPTED)
                return IS_CONNECTED;
            if (pid == ID_CONNECTION_ATTEMPT_FAILED || pid == ID_NO_FREE_INCOMING_CONNECTIONS
                || pid == ID_CONNECTION_BANNED || pid == ID_INVALID_PASSWORD
                || pid == ID_INCOMPATIBLE_PROTOCOL_VERSION)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "MasterQuery: master server unreachable (packet %d)", (int)pid);
                return IS_NOT_CONNECTED;
            }
        }

        ConnectionState state = mPeer->GetConnectionState(mMasterAddr);
        switch (state)
        {
            case IS_CONNECTED:
                return IS_CONNECTED;
            case IS_PENDING:
            case IS_CONNECTING:
                break;
            case IS_NOT_CONNECTED:
            case IS_DISCONNECTED:
            case IS_SILENTLY_DISCONNECTING:
            case IS_DISCONNECTING:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "MasterQuery: cannot connect to master server (state %d)", (int)state);
                return IS_NOT_CONNECTED;
        }
        RakSleep(500);
    }
}

int MasterQuery::waitForPacket(int waitingPacket,
                                std::map<SystemAddress, QueryData>* servers,
                                std::pair<SystemAddress, QueryData>* single)
{
    Packet* packet;
    int id = -1;
    bool done = false;

    while (!done)
    {
        for (packet = mPeer->Receive(); packet;
             mPeer->DeallocatePacket(packet), packet = mPeer->Receive())
        {
            BitStream data(packet->data, packet->length, false);
            mPmq->SetReadStream(&data);
            mPmu->SetReadStream(&data);

            unsigned char pid = 0;
            data.Read(pid);

            switch (pid)
            {
                case ID_CONNECTION_LOST:
                case ID_DISCONNECTION_NOTIFICATION:
                    done = true;
                    break;
                case ID_MASTER_QUERY:
                    if (waitingPacket == ID_MASTER_QUERY && servers)
                        mPmq->Read();
                    id = pid;
                    done = true;
                    break;
                case ID_MASTER_UPDATE:
                    if (waitingPacket == ID_MASTER_UPDATE && single)
                        mPmu->Read();
                    id = pid;
                    done = true;
                    break;
                case ID_CONNECTION_REQUEST_ACCEPTED:
                    break;
                default:
                    break;
            }
        }
        if (!done)
            RakSleep(500);
    }
    return id;
}

// Ping all servers in parallel: send all pings at once, collect responses for
// TIMEOUT_MS, return map of "host:port" -> ping_ms (-1 = no response).
std::map<std::string, int> MasterQuery::pingServers(
    const std::vector<std::pair<std::string, unsigned short>>& targets)
{
    static const RakNet::TimeMS TIMEOUT_MS = 2000;

    std::map<std::string, int> results;
    if (targets.empty())
        return results;

    for (auto& t : targets)
        results[t.first + ":" + std::to_string(t.second)] = -1;

    RakNet::SocketDescriptor sd{0, ""};
    RakNet::RakPeerInterface* peer = RakNet::RakPeerInterface::GetInstance();
    peer->Startup(1, &sd, 1);

    // Fire all pings
    for (auto& t : targets)
        peer->Ping(t.first.c_str(), t.second, false);

    RakNet::TimeMS start = RakNet::GetTimeMS();
    int remaining = (int)targets.size();

    while (remaining > 0)
    {
        RakNet::TimeMS now = RakNet::GetTimeMS();
        if (now - start >= TIMEOUT_MS)
            break;

        RakNet::Packet* packet = peer->Receive();
        if (!packet)
        {
            RakSleep(10);
            continue;
        }

        if (packet->data[0] == ID_UNCONNECTED_PONG)
        {
            RakNet::BitStream bs(&packet->data[1], packet->length - 1, false);
            RakNet::TimeMS sendTime;
            bs.Read(sendTime);
            int pingMs = (int)(now - sendTime);

            std::string host = packet->systemAddress.ToString(false);
            unsigned short port = packet->systemAddress.GetPort();
            std::string key = host + ":" + std::to_string(port);

            auto it = results.find(key);
            if (it != results.end() && it->second == -1)
            {
                it->second = pingMs;
                --remaining;
            }
        }
        peer->DeallocatePacket(packet);
    }

    peer->Shutdown(0);
    RakNet::RakPeerInterface::DestroyInstance(peer);
    return results;
}

} // namespace mwmp
