add_library(tes3mp-persistence STATIC
    AtomicFile.cpp
    PersistenceService.cpp
)

target_include_directories(tes3mp-persistence PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-persistence PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(tes3mp-persistence PUBLIC Threads::Threads)

if (MSVC)
    target_compile_options(tes3mp-persistence PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-persistence PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
