add_library(mwnet-transport STATIC
    ApplicationPacketBridge.cpp
    ApplicationPacketDispatcher.cpp
    ApplicationPacketReceiver.cpp
    SnapshotSequenceTracker.cpp
    TransportCodec.cpp
    TransportQueue.cpp
)

target_include_directories(mwnet-transport PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mwnet-transport PUBLIC cxx_std_20)
target_link_libraries(mwnet-transport PUBLIC mwnet-metrics mwnet-protocol-codec)

if (MSVC)
    target_compile_options(mwnet-transport PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if (MWNET_GNS_TARGET)
    add_library(mwnet-gns-transport STATIC
        GameNetworkingSocketsTransport.cpp
        SecureTransport.cpp
    )

    target_include_directories(mwnet-gns-transport PUBLIC ${CMAKE_SOURCE_DIR})
    target_compile_features(mwnet-gns-transport PUBLIC cxx_std_20)
    target_link_libraries(mwnet-gns-transport PUBLIC mwnet-transport mwnet-security ${MWNET_GNS_TARGET})

    if (MSVC)
        target_compile_options(mwnet-gns-transport PRIVATE /W4 /WX)
    else()
        target_compile_options(mwnet-gns-transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
