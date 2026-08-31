#include <components/openmw-mp/NetworkMessages.hpp>
#include "SystemPacket.hpp"

using namespace mwmp;

SystemPacket::SystemPacket() : BasePacket()
{
    packetID = 0;
}

SystemPacket::~SystemPacket()
{

}

void SystemPacket::setSystem(BaseSystem *newSystem)
{
    system = newSystem;
    guid = system->guid;
}

BaseSystem *SystemPacket::getSystem()
{
    return system;
}

bool SystemPacket::beginDecodeTransaction()
{
    return mDecodeTransaction.begin(system);
}

void SystemPacket::commitDecodeTransaction() noexcept
{
    mDecodeTransaction.commit(system);
}

void SystemPacket::rollbackDecodeTransaction() noexcept
{
    mDecodeTransaction.rollback(system);
}
