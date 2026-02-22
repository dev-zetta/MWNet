#ifndef OPENMW_VERSION_HPP
#define OPENMW_VERSION_HPP

#define TES3MP_VERSION "0.8.1"
#define TES3MP_PROTO_VERSION 10

// Commit hash used in the RakNet connection password. Must match on client and server.
// Empty string matches original TES3MP 0.8.1 public servers.
#ifndef TES3MP_COMPAT_COMMITHASH
#define TES3MP_COMPAT_COMMITHASH ""
#endif

#define TES3MP_DEFAULT_PASSW "blankpassword"
#define TES3MP_MASTERSERVER_PASSW "12345"


#endif //OPENMW_VERSION_HPP
