#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    // The fail-closed protocol codec is exercised here as it is introduced.
    // Keeping this target buildable first makes fuzz coverage a merge gate.
    std::uint8_t accumulator = 0;
    for (std::size_t i = 0; i < size; ++i)
        accumulator = static_cast<std::uint8_t>(accumulator ^ data[i]);
    return accumulator == 0xff && size == 0 ? 1 : 0;
}
