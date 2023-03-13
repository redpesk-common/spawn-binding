###########################################################################
# Copyright 2021 IoT.bzh
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###########################################################################

# Add target to project dependency list
PROJECT_TARGET_ADD(spawn)

	# compile any C file starting with a lower case
        set(CSOURCES
                ctl-lib.c
                spawn-childexec.c
                spawn-encoders.c
                spawn-expand.c
                spawn-sandbox.c
                spawn-utils.c
                spawn-binding.c
                spawn-config.c
                spawn-enums.c
                spawn-expand-defs.c
                spawn-subtask.c
                )
        set(CMAKE_HEADERS
                spawn-binding.h
                spawn-config.h
                spawn-defaults.h
                spawn-encoders.h
                spawn-encoders-plugins.h
                spawn-enums.h
                spawn-expand.h
                spawn-expand-defs.h
                spawn-sandbox.h
                spawn-subtask.h
                spawn-subtask-internal.h
                spawn-utils.h
                )

	# Define project Targets
	add_library(${TARGET_NAME} MODULE  ${CSOURCES})

    #target_compile_options(${TARGET_NAME}
    #    PUBLIC  -Wno-unused-variable
    #)

	# Binder exposes a unique public entry point
	SET_TARGET_PROPERTIES(${TARGET_NAME} PROPERTIES
		PREFIX "afb-"
		LABELS "LIBRARY"
		LINK_FLAGS ${BINDINGS_LINK_FLAG}
		IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/*${TARGET_NAME}.so
		OUTPUT_NAME ${TARGET_NAME}
	)

INSTALL(FILES ${CHEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

include(GNUInstallDirs)
configure_file(../${TARGET_NAME}.pc.in ${TARGET_NAME}.pc @ONLY)

INSTALL(FILES
		${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.pc
		DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/pkgconfig
	)


add_subdirectory("plugins")
