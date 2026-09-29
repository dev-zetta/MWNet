add_library(mwnet-protocol-codec STATIC
    EndpointSecurity.cpp
    MessageType.cpp
    PacketCodec.cpp
    RateLimits.cpp
)

target_include_directories(mwnet-protocol-codec PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mwnet-protocol-codec PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(mwnet-protocol-codec PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-protocol-codec PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
