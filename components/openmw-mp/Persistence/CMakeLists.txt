add_library(tes3mp-persistence STATIC
    AtomicFile.cpp
)

target_include_directories(tes3mp-persistence PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-persistence PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(tes3mp-persistence PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-persistence PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
