#ifndef OPENMW_BASECLIENTPACKETPROCESSOR_HPP
#define OPENMW_BASECLIENTPACKETPROCESSOR_HPP

#include <components/openmw-mp/Base/BasePacketProcessor.hpp>
#include "../LocalPlayer.hpp"
#include "../DedicatedPlayer.hpp"

namespace mwmp
{
    class BaseClientPacketProcessor
    {
    public:
    protected:
        inline bool isRequest()
        {
            return request;
        }

        inline bool isLocal()
        {
            return guid == myGuid;
        }

        LocalPlayer *getLocalPlayer();

    protected:
        static mwmp::transport::TransportConnectionId guid, myGuid;
        static bool request;
    };
}

#endif //OPENMW_BASECLIENTPACKETPROCESSOR_HPP
