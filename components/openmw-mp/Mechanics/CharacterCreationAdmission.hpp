#ifndef MWNET_CHARACTER_CREATION_ADMISSION_HPP
#define MWNET_CHARACTER_CREATION_ADMISSION_HPP

namespace mwmp::mechanics
{
    // Owned by the server Player, never decoded from BasePlayer wire state.
    class CharacterCreationAdmission
    {
    public:
        bool begin(int firstStage, int endStage) noexcept
        {
            if (firstStage < 0 || firstStage >= endStage || endStage < 2 || endStage > 5)
                return false;
            mFirstStage = firstStage;
            mEndStage = endStage;
            mActive = true;
            return true;
        }
        bool active() const noexcept { return mActive; }
        bool accepts(int stage, int endStage) const noexcept
        {
            return mActive && endStage == mEndStage && stage >= mFirstStage && stage <= mEndStage;
        }
        bool complete(int stage, int endStage) noexcept
        {
            if (!accepts(stage, endStage) || stage != mEndStage) return false;
            mActive = false;
            return true;
        }
        void cancel() noexcept { mActive = false; }

    private:
        bool mActive = false;
        int mFirstStage = 0;
        int mEndStage = 0;
    };
}

#endif
