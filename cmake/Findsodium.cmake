# Locate libsodium and expose the conventional sodium and sodium::sodium targets.

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(PC_sodium QUIET libsodium)
endif()

find_path(sodium_INCLUDE_DIR
    NAMES sodium.h
    HINTS ${PC_sodium_INCLUDE_DIRS}
)
find_library(sodium_LIBRARY
    NAMES sodium libsodium
    HINTS ${PC_sodium_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(sodium
    REQUIRED_VARS sodium_LIBRARY sodium_INCLUDE_DIR
    VERSION_VAR PC_sodium_VERSION
)

if (sodium_FOUND AND NOT TARGET sodium)
    add_library(sodium UNKNOWN IMPORTED)
    set_target_properties(sodium PROPERTIES
        IMPORTED_LOCATION "${sodium_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${sodium_INCLUDE_DIR}"
    )
endif()

if (TARGET sodium AND NOT TARGET sodium::sodium)
    add_library(sodium::sodium ALIAS sodium)
endif()

mark_as_advanced(sodium_INCLUDE_DIR sodium_LIBRARY)
