#include <components/openmw-mp/Script/NativeFunction.hpp>

#include <cstdint>
#include <iostream>
#include <type_traits>

namespace
{
    template <typename T>
    using Storage = std::conditional_t<std::is_pointer_v<T>, void*,
        std::conditional_t<std::is_same_v<T, bool>, bool,
            std::conditional_t<std::is_floating_point_v<T>, double,
                std::conditional_t<std::is_signed_v<T>,
                    std::conditional_t<(sizeof(T) > 4), long long, int>,
                    std::conditional_t<(sizeof(T) > 4), unsigned long long, unsigned int>>>>>;

    using NativeFunction = mwmp::script::NativeFunction<Storage>;

    double mixed(std::uint8_t a, std::int16_t b, bool c, double d, const char* text,
        int* pointer, std::uint64_t wide, int e, int f, std::uint8_t g,
        std::int16_t h, std::uint8_t i, double j)
    {
        return a == 231 && b == -1234 && c && d == 1.25 && text[0] == 'M'
                && *pointer == 77 && wide == 0x123456789abcdef0ULL && e == 11
                && f == 12 && g == 245 && h == -3210 && i == 219 && j == 2.5
            ? 123.5 : -1.0;
    }

    bool sCalled = false;
    void setFlag(bool value, std::uint8_t number) { sCalled = value && number == 231; }
    std::uint8_t smallReturn() { return 231; }
    std::int16_t signedReturn() { return -1234; }
    const char* echo(const char* value) { return value; }
    struct Opaque { int value; };
    Opaque opaqueReturn() { return { 42 }; }
}

int runNativeFunctionTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition)
        {
            std::cerr << "native_function.cpp: " << message << '\n';
            ++failures;
        }
    };

    int pointed = 77;
    void* pointer = &pointed;
    const char text[] = "MWNet";
    void* scriptText = const_cast<char*>(text);
    NativeFunction function(&mixed);
    expect(function.call<double>(231u, -1234, true, 1.25, scriptText, pointer,
        0x123456789abcdef0ULL, 11, 12, 245u, -3210, 219u, 2.5) == 123.5,
        "mixed register/stack arguments retain their exact native types");
    NativeFunction(&setFlag).call<void>(true, 231u);
    expect(sCalled, "void function receives boolean and narrow integer arguments");
    expect(NativeFunction(&smallReturn).call<unsigned int>() == 231,
        "unsigned narrow return is normalized");
    expect(NativeFunction(&signedReturn).call<int>() == -1234,
        "signed narrow return is normalized");
    expect(NativeFunction(&echo).call<void*>(scriptText) == scriptText,
        "pointer arguments and return values survive erasure");
    NativeFunction opaque(&opaqueReturn);
    expect(opaque.address() != nullptr, "custom adapters retain their native address");
    bool rejected = false;
    try
    {
        opaque.call<unsigned int>();
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    expect(rejected, "opaque results require a custom language adapter");
    return failures;
}
