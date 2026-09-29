#ifndef TMPTYPES_HPP
#define TMPTYPES_HPP


#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <components/openmw-mp/Script/NativeFunction.hpp>
#include "Utils.hpp"

#ifdef _WIN32
#include <winsock2.h>
#endif

#ifdef _WIN32
typedef HMODULE lib_t;
#else
typedef void* lib_t;
#endif

template<typename T> struct sizeof_void { enum { value = sizeof(T) }; };
template<> struct sizeof_void<void> { enum { value = 0 }; };


template<typename T, size_t t> struct TypeChar { static_assert(!t, "Unsupported type in variadic type list"); };
template<> struct TypeChar<bool, sizeof(bool)> { enum { value = 'b' }; };
template<typename T> struct TypeChar<T*, sizeof(void*)> { enum { value = 'p' }; };
template<> struct TypeChar<double*, sizeof(double*)> { enum { value = 'd' }; };
template<typename T> struct TypeChar<T, sizeof(uint8_t)> { enum { value = std::is_signed<T>::value ? 'q' : 'i' }; };
template<typename T> struct TypeChar<T, sizeof(uint16_t)> { enum { value = std::is_signed<T>::value ? 'q' : 'i' }; };
template<typename T> struct TypeChar<T, sizeof(uint32_t)> { enum { value = std::is_signed<T>::value ? 'q' : 'i' }; };
template<typename T> struct TypeChar<T, sizeof(uint64_t)> { enum { value = std::is_signed<T>::value ? 'w' : 'l' }; };
template<> struct TypeChar<double, sizeof(double)> { enum { value = 'f' }; };
template<> struct TypeChar<char*, sizeof(char*)> { enum { value = 's' }; };
template<> struct TypeChar<const char*, sizeof(const char*)> { enum { value = 's' }; };
template<> struct TypeChar<void, sizeof_void<void>::value> { enum { value = 'v' }; };

template<const char t> struct CharType { static_assert(!t, "Unsupported type in variadic type list"); };
template<> struct CharType<'b'> { typedef bool type; };
template<> struct CharType<'p'> { typedef void* type; };
template<> struct CharType<'d'> { typedef double* type; };
template<> struct CharType<'q'> { typedef signed int type; };
template<> struct CharType<'i'> { typedef unsigned int type; };
template<> struct CharType<'w'> { typedef signed long long type; };
template<> struct CharType<'l'> { typedef unsigned long long type; };
template<> struct CharType<'f'> { typedef double type; };
template<> struct CharType<'s'> { typedef const char* type; };
template<> struct CharType<'v'> { typedef void type; };

template <typename T>
using ScriptValue = typename CharType<TypeChar<T, sizeof_void<T>::value>::value>::type;

template<typename... Types>
struct TypeString {
    static constexpr char value[sizeof...(Types) + 1] = {
            TypeChar<Types, sizeof(Types)>::value...
    };
};


template<typename R, typename... Types>
using Function = R(*)(Types...);

template<typename R>
using FunctionEllipsis = R(*)(...);

struct ScriptIdentity
{
    const char* types;
    const char ret;
    const unsigned int numargs;

    constexpr bool matches(const char* types, const unsigned int N = 0) const
    {
        return N < numargs ? this->types[N] == types[N] && matches(types, N + 1) : this->types[N] == types[N];
    }

    template<typename R, typename... Types>
    constexpr ScriptIdentity(Function<R, Types...>) : types(TypeString<Types...>::value), ret(TypeChar<R, sizeof_void<R>::value>::value), numargs(sizeof(TypeString<Types...>::value) - 1) {}
};

template<typename... Types>
using Callback = void (*)(Types...);

struct CallbackIdentity
{
    const char* types;
    const unsigned int numargs;

    constexpr bool matches(const char* types, const unsigned int N = 0) const
    {
        return N < numargs ? this->types[N] == types[N] && matches(types, N + 1) : this->types[N] == types[N];
    }

    template<typename... Types>
    constexpr CallbackIdentity(Callback<Types...>) : types(TypeString<Types...>::value), numargs(sizeof(TypeString<Types...>::value) - 1) {}
};


struct ScriptFunctionPointer : public ScriptIdentity
{
    mwmp::script::NativeFunction<ScriptValue> native;
    template<typename R, typename... Types>
    ScriptFunctionPointer(Function<R, Types...> a) : ScriptIdentity(a), native(a) {}
    void* voidAddr() const { return native.address(); }
};

// Signatures remain usable by Lua's compile-time dispatcher. Function address
// erasure belongs in the runtime table: reinterpret_cast is not constexpr.
struct ScriptFunctionMetadata
{
    const char* name;
    const ScriptIdentity func;

    template<typename R, typename... Types>
    constexpr ScriptFunctionMetadata(const char* name, Function<R, Types...> func)
        : name(name), func(func) {}
};

struct ScriptFunctionData
{
    const char* name;
    const ScriptFunctionPointer func;

    ScriptFunctionData(const char* name, ScriptFunctionPointer func) : name(name), func(func) {}
};

struct ScriptCallbackData
{
    const char* name;
    const unsigned int index;
    const CallbackIdentity callback;

    template<size_t N>
    constexpr ScriptCallbackData(const char(&name)[N], CallbackIdentity _callback) : name(name), index(Utils::hash(name)), callback(_callback) {}
};

#endif //TMPTYPES_HPP
