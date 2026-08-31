add_library(tes3mp-metrics STATIC
    ServerMetrics.cpp
)

target_include_directories(tes3mp-metrics PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-metrics PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(tes3mp-metrics PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-metrics PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
