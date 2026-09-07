#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/manualref.hpp"

#include <components/esm/records.hpp>
#include <components/esm3/readerscache.hpp>

#include <gtest/gtest.h>

namespace MWWorld
{
    namespace
    {
        TEST(MWWorldManualRefTest, insertedReferenceOutlivesTemporaryTemplate)
        {
            MWClass::Npc::registerSelf();

            ESM::NPC npc;
            npc.blank();
            npc.mId = ESM::RefId::stringRefId("replacement_npc");

            ESMStore store;
            store.insertStatic(npc);
            store.setUp();

            ESM::Cell esmCell;
            esmCell.blank();
            esmCell.mId = ESM::RefId::stringRefId("replacement_test_cell");
            ESM::ReadersCache readersCache;
            CellStore cellStore(Cell(esmCell), store, readersCache);

            ESM::Position expectedPosition;
            expectedPosition.pos[0] = 10.f;
            expectedPosition.pos[1] = 20.f;
            expectedPosition.pos[2] = 30.f;
            expectedPosition.rot[0] = 0.1f;
            expectedPosition.rot[1] = 0.2f;
            expectedPosition.rot[2] = 0.3f;

            Ptr inserted;
            {
                ManualRef temporary(store, npc.mId);
                temporary.getPtr().getRefData().setPosition(expectedPosition);
                inserted = Ptr(cellStore.insert(temporary.getPtr().get<ESM::NPC>()), &cellStore);
            }

            EXPECT_EQ(inserted.getRefData().getPosition(), expectedPosition);
            EXPECT_EQ(inserted.getCellRef().getRefId(), npc.mId);
        }
    }
}
