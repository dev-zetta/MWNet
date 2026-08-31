#ifndef OPENMW_BASESYSTEM_HPP
#define OPENMW_BASESYSTEM_HPP

#include <string>
#include <components/openmw-mp/Transport/ITransport.hpp>

namespace mwmp
{
    class BaseSystem
    {
    public:

        explicit BaseSystem(mwmp::transport::TransportConnectionId guid)
            : guid(guid)
        {
        }

        BaseSystem() = default;

        mwmp::transport::TransportConnectionId guid{};
        std::string playerName;
        std::string serverPassword;

    };
}

#endif //OPENMW_BASESYSTEM_HPP
