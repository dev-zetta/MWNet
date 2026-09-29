add_library(mwnet-session STATIC
    AuthorityLease.cpp
    SessionState.cpp
    SessionTransport.cpp
)

target_include_directories(mwnet-session PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mwnet-session PUBLIC cxx_std_20)
target_link_libraries(mwnet-session PUBLIC mwnet-protocol-codec mwnet-transport)

if (MSVC)
    target_compile_options(mwnet-session PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-session PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if (TARGET mwnet-gns-transport)
    add_library(mwnet-game-endpoint STATIC
        ../Transport/GameEndpoint.cpp
    )
    target_include_directories(mwnet-game-endpoint PUBLIC ${CMAKE_SOURCE_DIR})
    target_compile_features(mwnet-game-endpoint PUBLIC cxx_std_20)
    target_link_libraries(mwnet-game-endpoint
        PUBLIC mwnet-session mwnet-gns-transport)

    if (MSVC)
        target_compile_options(mwnet-game-endpoint PRIVATE /W4 /WX)
    else()
        target_compile_options(mwnet-game-endpoint
            PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
