include_guard(GLOBAL)

find_package(sodium REQUIRED)
find_package(GameNetworkingSockets CONFIG QUIET)

if (NOT TARGET GameNetworkingSockets::static AND NOT TARGET GameNetworkingSockets::shared)
    if (NOT TES3MP_FETCH_DEPS)
        message(FATAL_ERROR
            "GameNetworkingSockets was not found. Install v1.5.1 or configure with "
            "-DTES3MP_FETCH_DEPS=ON to fetch the pinned dependency revision.")
    endif()

    include(FetchContent)

    set(BUILD_STATIC_LIB ON CACHE BOOL "Build static GameNetworkingSockets" FORCE)
    set(BUILD_SHARED_LIB OFF CACHE BOOL "Build shared GameNetworkingSockets" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build GameNetworkingSockets examples" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "Build GameNetworkingSockets tests" FORCE)
    set(BUILD_TOOLS OFF CACHE BOOL "Build GameNetworkingSockets tools" FORCE)
    set(ENABLE_ICE OFF CACHE BOOL "Build GameNetworkingSockets ICE support" FORCE)
    set(USE_STEAMWEBRTC OFF CACHE BOOL "Build GameNetworkingSockets WebRTC support" FORCE)
    set(USE_CRYPTO OpenSSL CACHE STRING "GameNetworkingSockets crypto provider" FORCE)

    FetchContent_Declare(GameNetworkingSockets
        GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
        GIT_TAG fa489fd2cb0fc86ef2503e330935d3eb03a6a064
        GIT_SUBMODULES ""
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(GameNetworkingSockets)

    # GNS v1.5.1 intentionally erases typed packet callback pointers through a
    # void* adapter and performs unaligned integer access in its packet codec.
    # Clang UBSan rejects both constructs. Keep ASan enabled for the dependency,
    # while TES3MP itself remains fully ASan/UBSan instrumented.
    if (TES3MP_GNS_UBSAN_COMPAT AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(GameNetworkingSockets_s PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-fno-sanitize=undefined>)
    endif()
endif()

if (TARGET GameNetworkingSockets::static)
    set(TES3MP_GNS_TARGET GameNetworkingSockets::static)
elseif (TARGET GameNetworkingSockets::shared)
    set(TES3MP_GNS_TARGET GameNetworkingSockets::shared)
else()
    message(FATAL_ERROR "The GameNetworkingSockets package did not export a supported target")
endif()

set(TES3MP_GNS_TARGET "${TES3MP_GNS_TARGET}" CACHE INTERNAL "GameNetworkingSockets target used by TES3MP")
