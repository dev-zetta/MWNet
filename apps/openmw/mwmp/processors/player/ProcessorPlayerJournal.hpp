#ifndef OPENMW_PROCESSORPLAYERJOURNAL_HPP
#define OPENMW_PROCESSORPLAYERJOURNAL_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerJournal final: public PlayerProcessor
    {
    public:
        ProcessorPlayerJournal()
        {
            BPP_INIT(ID_PLAYER_JOURNAL)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_JOURNAL from server");

            if (isRequest())
            {
                // Entire journal cannot currently be requested from players
            }
            else if (player != nullptr)
            {
                static_cast<LocalPlayer*>(player)->addJournalItems();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERJOURNAL_HPP
