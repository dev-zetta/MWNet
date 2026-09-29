add_library(mwnet-metrics STATIC
    ProcessMemory.cpp
    ServerMetrics.cpp
)

target_include_directories(mwnet-metrics PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mwnet-metrics PUBLIC cxx_std_20)

if (WIN32)
    target_link_libraries(mwnet-metrics PRIVATE psapi)
endif()

if (MSVC)
    target_compile_options(mwnet-metrics PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-metrics PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
