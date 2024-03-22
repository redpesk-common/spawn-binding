CMAKE_MINIMUM_REQUIRED(VERSION 3.12)

# Project info
project(spawn-binding
    VERSION 2.0.0
    DESCRIPTION "Sandboxed execution of predefined commands"
    HOMEPAGE_URL "https://github.com/redpesk-common/spawn-binding"
    LANGUAGES C
)
set(PROJECT_AUTHOR "Iot-Team")
set(PROJECT_AUTHOR_MAIL "support@redpesk.bzh")
set(PROJECT_LICENSE "GPL3.0")

include(FindPkgConfig)
include(GNUInstallDirs)
include(CheckIncludeFile)

# Options
set(AFM_APP_DIR ${CMAKE_INSTALL_PREFIX}/redpesk CACHE PATH "Applications directory")
set(APP_DIR ${AFM_APP_DIR}/${PROJECT_NAME})

# Check dependencies
pkg_check_modules(deps REQUIRED
    json-c
    libcap-ng
    libseccomp
    afb-binding>=4.1.2
    librp-utils-static
    afb-helpers4-static>=10.0.3
)
check_include_file(uthash.h check_uthash)

# Build included libraries
add_library(spawn-binding-libs SHARED)
set_target_properties(spawn-binding-libs PROPERTIES OUTPUT_NAME spawn-binding)
target_sources(spawn-binding-libs PRIVATE
    src/lib/jsonc-buf.c
    src/lib/line-buf.c
    src/lib/stream-buf.c
    src/lib/vfmt.c
)
# Install included libraries
install(TARGETS spawn-binding-libs DESTINATION ${APP_DIR}/lib)
install(DIRECTORY src/lib/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spawn-binding/lib FILES_MATCHING PATTERN "*.h")

# Build spawn-binding
add_library(spawn-binding SHARED)
set_target_properties(spawn-binding PROPERTIES PREFIX "")
target_sources(spawn-binding PRIVATE
    src/spawn-binding.c
    src/spawn-childexec.c
    src/spawn-config.c
    src/spawn-encoders.c
    src/spawn-enums.c
    src/spawn-expand.c
    src/spawn-expand-defs.c
    src/spawn-sandbox.c
    src/spawn-subtask.c
    src/spawn-utils.c
)
target_link_libraries(spawn-binding PRIVATE ${deps_LIBRARIES} spawn-binding-libs)
configure_file(spawn.pc.in ${CMAKE_BINARY_DIR}/spawn.pc @ONLY)
configure_file(manifest.yml.in ${CMAKE_BINARY_DIR}/manifest.yml @ONLY)
# Install spawn-binding
install(TARGETS spawn-binding DESTINATION ${APP_DIR}/lib)
install(FILES ${CMAKE_BINARY_DIR}/manifest.yml DESTINATION ${APP_DIR}/.rpconfig)
install(FILES ${CMAKE_BINARY_DIR}/spawn.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(DIRECTORY etc/ DESTINATION ${APP_DIR}/etc)
install(FILES src/spawn-binding.h src/spawn-defaults.h src/spawn-encoders.h src/spawn-enums.h src/spawn-sandbox.h src/spawn-subtask.h src/spawn-utils.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spawn-binding
)

# Build encoder-sample
add_library(encoder-sample SHARED src/plugins/encoder-sample.c)
set_target_properties(encoder-sample PROPERTIES PREFIX "")
target_include_directories(encoder-sample PRIVATE src)
target_link_libraries(encoder-sample spawn-binding spawn-binding-libs)
# Install encoder-sample
install(TARGETS encoder-sample DESTINATION ${APP_DIR}/lib/plugins)
