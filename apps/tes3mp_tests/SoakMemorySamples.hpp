#ifndef TES3MP_TESTS_SOAK_MEMORY_SAMPLES_HPP
#define TES3MP_TESTS_SOAK_MEMORY_SAMPLES_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace tes3mp::tests
{
    // Keep the observer's memory bounded even when a duration-based soak runs
    // for more cycles than requested. A growing vector changes the RSS being
    // measured; reserving an estimated cycle count only postpones that defect.
    class SoakMemorySamples
    {
    public:
        explicit SoakMemorySamples(std::filesystem::path path)
            : mPath(std::move(path))
        {
            if (!mPath.parent_path().empty())
                std::filesystem::create_directories(mPath.parent_path());
            mOutput.open(mPath, std::ios::binary | std::ios::trunc);
            if (!mOutput)
                throw std::runtime_error("failed to create RSS sample journal");
        }

        void append(std::uint64_t bytes)
        {
            mOutput.write(reinterpret_cast<const char*>(&bytes), sizeof(bytes));
            if (!mOutput)
                throw std::runtime_error("failed to write RSS sample journal");
            ++mCount;
        }

        std::size_t size() const { return mCount; }

        double growthPercentAfterWarmup()
        {
            if (mCount < 20)
                return 0;
            const std::size_t warmup = mCount / 4;
            const std::size_t window = std::max<std::size_t>(5, mCount / 10);
            long double early = 0;
            long double late = 0;
            visit([&](std::size_t index, std::uint64_t bytes) {
                if (index >= warmup && index < warmup + window)
                    early += bytes;
                if (index >= mCount - window)
                    late += bytes;
            });
            // Both windows have the same count, so the ratio of their sums is
            // the ratio of their means. Preserve the existing warm-up policy.
            return early > 0 ? static_cast<double>((late - early) / early * 100.0L) : 0;
        }

        void writeJsonArray(std::ostream& output)
        {
            output << '[';
            visit([&](std::size_t index, std::uint64_t bytes) {
                if (index != 0)
                    output << ", ";
                output << bytes;
            });
            output << ']';
        }

    private:
        template <class Visitor>
        void visit(Visitor visitor)
        {
            mOutput.flush();
            if (!mOutput)
                throw std::runtime_error("failed to flush RSS sample journal");
            std::ifstream input(mPath, std::ios::binary);
            if (!input)
                throw std::runtime_error("failed to open RSS sample journal");
            for (std::size_t index = 0; index < mCount; ++index)
            {
                std::uint64_t bytes = 0;
                if (!input.read(reinterpret_cast<char*>(&bytes), sizeof(bytes)))
                    throw std::runtime_error("failed to read RSS sample journal");
                visitor(index, bytes);
            }
        }

        std::filesystem::path mPath;
        std::ofstream mOutput;
        std::size_t mCount = 0;
    };
}

#endif
