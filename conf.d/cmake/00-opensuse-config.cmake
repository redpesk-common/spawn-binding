message(STATUS "Custom options: 00-opensuse-config.cmake --")
add_definitions(-DSUSE_LUA_INCDIR)
list(APPEND PKG_REQUIRED_LIST lua>=5.3)
list(APPEND PKG_REQUIRED_LIST uthash)

# Libshell is not part of standard Linux Distro (https://libshell.org/)
set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib64/pkgconfig")

# memfd_create not present even on OpenSuse-15.2
add_definitions(-DMEMFD_CREATE_MISSING)
