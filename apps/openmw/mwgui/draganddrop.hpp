#ifndef OPENMW_MWGUI_DRAGANDDROP_H
#define OPENMW_MWGUI_DRAGANDDROP_H

#include "itemmodel.hpp"
#include "itemwidget.hpp"

#include <cstddef>

namespace MyGUI
{
    class Widget;
}

namespace MWGui
{

    class ItemView;
    class SortFilterItemModel;

    class DragAndDrop
    {
    public:
        bool mIsOnDragAndDrop;
        ItemWidget* mDraggedWidget;
        ItemModel* mSourceModel;
        ItemView* mSourceView;
        SortFilterItemModel* mSourceSortModel;
        ItemStack mItem;
        std::size_t mDraggedCount;

        DragAndDrop();

        void startDrag(int index, SortFilterItemModel* sortModel, ItemModel* sourceModel, ItemView* sourceView,
            std::size_t count, bool playSound = true);
        void drop(ItemModel* targetModel, ItemView* targetView, bool playSound = true);
        void update();
        void onFrame();

        /*
            Start of tes3mp change (minor)

            Add a deleteDragItems argument that allows the deletion of the
            items in the drag as oppposed to the regular behavior of returning
            them to their source model

            This is required to reduce unpredictable behavior for drags approved
            or rejected by the server
        */
        void finish(bool deleteDragItems = false);
        /* End of tes3mp change (minor) */
    };

}

#endif
