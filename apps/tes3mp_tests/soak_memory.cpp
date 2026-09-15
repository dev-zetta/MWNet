#include "SoakMemorySamples.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

int runSoakMemoryTests()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("tes3mp-memory-samples-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition)
        {
            ++failures;
            std::cerr << "soak_memory.cpp: " << message << '\n';
        }
    };
    try
    {
        // Include the failed soak's sample count and cross both vector growth
        // boundaries that perturbed its RSS. Compare against the old formula.
        for (const std::size_t count : {0, 19, 20, 23, 2049, 4097, 5632, 10000})
        {
            tes3mp::tests::SoakMemorySamples samples(root / "samples.bin");
            std::vector<std::uint64_t> reference;
            std::ostringstream expectedJson;
            expectedJson << '[';
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::uint64_t value = 10'000'000 + i * 100;
                samples.append(value);
                reference.push_back(value);
                if (i != 0)
                    expectedJson << ", ";
                expectedJson << value;
            }
            expectedJson << ']';
            double expectedGrowth = 0;
            if (count >= 20)
            {
                const auto warmup = count / 4;
                const auto window = std::max<std::size_t>(5, count / 10);
                const long double early = std::accumulate(reference.begin() + warmup,
                    reference.begin() + warmup + window, 0.0L) / window;
                const long double late = std::accumulate(reference.end() - window,
                    reference.end(), 0.0L) / window;
                expectedGrowth = static_cast<double>((late - early) / early * 100.0L);
            }
            expect(samples.size() == count, "sample count changed");
            expect(std::abs(samples.growthPercentAfterWarmup() - expectedGrowth) < 1e-12,
                "growth policy differs from the original window means");
            std::ostringstream json;
            samples.writeJsonArray(json);
            expect(json.str() == expectedJson.str(), "JSON lost or reordered samples");
            expect(std::filesystem::file_size(root / "samples.bin") == count * sizeof(std::uint64_t),
                "journal did not retain every sample");
        }
        for (const std::uint64_t increase : {0, 1, 2})
        {
            tes3mp::tests::SoakMemorySamples samples(root / "samples.bin");
            for (std::size_t i = 0; i < 100; ++i)
                samples.append(i < 50 ? 100 : 100 + increase);
            expect((samples.growthPercentAfterWarmup() > 1.0) == (increase > 1),
                "1% threshold acceptance changed");
        }
        tes3mp::tests::SoakMemorySamples samples(root / "samples.bin");
        for (std::size_t i = 0; i < 20; ++i)
            samples.append(0);
        expect(samples.growthPercentAfterWarmup() == 0, "zero RSS changed behavior");
        std::filesystem::resize_file(root / "samples.bin", sizeof(std::uint64_t));
        bool rejected = false;
        try { (void)samples.growthPercentAfterWarmup(); }
        catch (const std::runtime_error&) { rejected = true; }
        expect(rejected, "truncated journal was silently accepted");
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "soak_memory.cpp: " << error.what() << '\n';
    }
    std::filesystem::remove_all(root);
    return failures;
}
