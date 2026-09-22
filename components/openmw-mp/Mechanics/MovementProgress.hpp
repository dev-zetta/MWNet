#ifndef OPENMW_MP_MOVEMENT_PROGRESS_HPP
#define OPENMW_MP_MOVEMENT_PROGRESS_HPP

#include "MovementValidator.hpp"
#include <cmath>

namespace mwmp::mechanics
{
    // A displacement window also catches actors oscillating around an obstacle.
    class MovementProgress
    {
    public:
        MovementProgress(double radius = 64, double timeout = 20)
            : mRadius(radius), mTimeout(timeout) {}
        bool update(Position3 position, bool tryingToMove, double elapsed)
        {
            if (!tryingToMove || !std::isfinite(elapsed) || elapsed < 0
                || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            {
                reset();
                return false;
            }
            if (!mStarted || distance(position, mOrigin) > mRadius)
            {
                mStarted = true;
                mOrigin = position;
                mElapsed = 0;
                return false;
            }
            mElapsed += elapsed;
            return mElapsed >= mTimeout;
        }
        void reset() { mStarted = false; mElapsed = 0; }
        static double distance(Position3 a, Position3 b)
        {
            return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
        }
    private:
        double mRadius;
        double mTimeout;
        Position3 mOrigin{};
        double mElapsed = 0;
        bool mStarted = false;
    };
}
#endif
