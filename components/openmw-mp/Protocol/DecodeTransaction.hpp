#ifndef OPENMW_MP_PROTOCOL_DECODE_TRANSACTION_HPP
#define OPENMW_MP_PROTOCOL_DECODE_TRANSACTION_HPP

#include <optional>
#include <type_traits>
#include <utility>

namespace mwmp::protocol
{
    template <class Model>
    class DecodeTransaction
    {
    public:
        static_assert(std::is_copy_constructible_v<Model>,
            "packet models must be copyable to support atomic decode");
        static_assert(std::is_nothrow_move_assignable_v<Model>,
            "packet models must be nothrow move assignable to support atomic commit");

        bool begin(Model*& active)
        {
            if (active == nullptr || mTarget != nullptr)
                return false;

            mTarget = active;
            try
            {
                mStaged.emplace(*active);
            }
            catch (...)
            {
                mTarget = nullptr;
                throw;
            }
            active = &*mStaged;
            return true;
        }

        void commit(Model*& active) noexcept
        {
            if (mTarget == nullptr || !mStaged)
                return;

            Model* target = mTarget;
            *target = std::move(*mStaged);
            active = target;
            clear();
        }

        void rollback(Model*& active) noexcept
        {
            if (mTarget != nullptr)
                active = mTarget;
            clear();
        }

        bool active() const noexcept { return mTarget != nullptr; }

    private:
        void clear() noexcept
        {
            mStaged.reset();
            mTarget = nullptr;
        }

        Model* mTarget = nullptr;
        std::optional<Model> mStaged;
    };
}

#endif
