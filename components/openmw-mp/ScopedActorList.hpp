#ifndef OPENMW_MP_SCOPED_ACTOR_LIST_HPP
#define OPENMW_MP_SCOPED_ACTOR_LIST_HPP

#include "Base/BaseActor.hpp"

#include <utility>

namespace mwmp
{
    // Script readers share one received list. Publishing a derived event must
    // preserve the original batch and references held by its packet processor.
    class ScopedActorList
    {
    public:
        ScopedActorList(BaseActorList& shared, BaseActorList& event)
            : mShared(shared), mEvent(event)
        {
            std::swap(mShared, mEvent);
        }

        ~ScopedActorList() { std::swap(mShared, mEvent); }
        ScopedActorList(const ScopedActorList&) = delete;
        ScopedActorList& operator=(const ScopedActorList&) = delete;

    private:
        BaseActorList& mShared;
        BaseActorList& mEvent;
    };
}

#endif
