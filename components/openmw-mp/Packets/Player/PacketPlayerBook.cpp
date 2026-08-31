#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerBook.hpp"

using namespace mwmp;

PacketPlayerBook::PacketPlayerBook() : PlayerPacket()
{
    packetID = ID_PLAYER_BOOK;
}

void PacketPlayerBook::Packet(bool send)
{
    PlayerPacket::Packet(send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->bookChanges.size());

    if (!RWCount(count, send))
        return;

    if (!send)
    {
        player->bookChanges.clear();
        player->bookChanges.resize(count);
    }

    for (auto &&book : player->bookChanges)
    {
        RW(book.bookId, send, true);
    }
}
