#ifndef OPENMW_BASESYSTEM_HPP
#define OPENMW_BASESYSTEM_HPP

#include <string>

#include <RakNetTypes.h>

namespace mwmp
{
    class BaseSystem
    {
    public:

        explicit BaseSystem(RakNet::RakNetGUID guid)
            : guid(guid)
        {
        }

        BaseSystem() = default;

        RakNet::RakNetGUID guid{};
        std::string playerName;
        std::string serverPassword;

    };
}

#endif //OPENMW_BASESYSTEM_HPP
