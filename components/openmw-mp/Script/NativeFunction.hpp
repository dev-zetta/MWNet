#ifndef MWNET_SCRIPT_NATIVE_FUNCTION_HPP
#define MWNET_SCRIPT_NATIVE_FUNCTION_HPP

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace mwmp::script
{
    // Storage describes the script API's normalized value types. Retain an
    // invocation thunk with the original signature so erasure never changes
    // the native calling convention (in particular, fixed versus varargs).
    template <template <typename> typename Storage>
    class NativeFunction
    {
    public:
        template <typename R, typename... Args>
        explicit NativeFunction(R (*function)(Args...))
            : mAddress(reinterpret_cast<std::uintptr_t>(function))
            , mInvoke(nullptr)
        {
            // Opaque return values (such as CallPublic's boost::any) use a
            // custom language adapter, while native plugins still need the address.
            if constexpr (std::is_void_v<R> || std::is_arithmetic_v<R>
                || std::is_enum_v<R> || std::is_pointer_v<R>)
                mInvoke = &invoke<R, Args...>;
        }

        void* address() const { return reinterpret_cast<void*>(mAddress); }

        template <typename R, typename... Args>
        R call(const Args&... args) const
        {
            if (mInvoke == nullptr)
                throw std::logic_error("Native function requires a custom script adapter");
            const void* values[] = { std::addressof(args)..., nullptr };
            if constexpr (std::is_void_v<R>)
                mInvoke(mAddress, nullptr, values);
            else
            {
                R result{};
                mInvoke(mAddress, &result, values);
                return result;
            }
        }

    private:
        template <typename To, typename From>
        static To convert(From value)
        {
            if constexpr (std::is_pointer_v<To> && std::is_pointer_v<From>)
                return reinterpret_cast<To>(const_cast<void*>(reinterpret_cast<const void*>(value)));
            else
                return static_cast<To>(value);
        }

        template <typename R, typename... Args, std::size_t... I>
        static void invokeTyped(std::uintptr_t address, void* result,
            const void* const* values, std::index_sequence<I...>)
        {
            auto function = reinterpret_cast<R (*)(Args...)>(address);
            if constexpr (std::is_void_v<R>)
                function(convert<Args>(*static_cast<const Storage<Args>*>(values[I]))...);
            else
                *static_cast<Storage<R>*>(result) = convert<Storage<R>>(
                    function(convert<Args>(*static_cast<const Storage<Args>*>(values[I]))...));
        }

        template <typename R, typename... Args>
        static void invoke(std::uintptr_t address, void* result, const void* const* values)
        {
            invokeTyped<R, Args...>(address, result, values, std::index_sequence_for<Args...>{});
        }

        std::uintptr_t mAddress;
        void (*mInvoke)(std::uintptr_t, void*, const void* const*);
    };
}

#endif
