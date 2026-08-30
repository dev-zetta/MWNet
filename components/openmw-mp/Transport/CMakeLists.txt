add_library(tes3mp-transport STATIC
    SnapshotSequenceTracker.cpp
    TransportCodec.cpp
    TransportQueue.cpp
)

target_include_directories(tes3mp-transport PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-transport PUBLIC cxx_std_20)
target_link_libraries(tes3mp-transport PUBLIC tes3mp-protocol-codec)

if (MSVC)
    target_compile_options(tes3mp-transport PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
