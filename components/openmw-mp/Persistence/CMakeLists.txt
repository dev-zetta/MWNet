add_library(mwnet-persistence STATIC
    AtomicFile.cpp
    PersistenceService.cpp
)

target_include_directories(mwnet-persistence PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mwnet-persistence PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(mwnet-persistence PUBLIC Threads::Threads)

if (MSVC)
    target_compile_options(mwnet-persistence PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-persistence PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
