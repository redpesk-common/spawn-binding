
PROJECT_TARGET_ADD(encoder-sample)

	# compile any C file starting with a lower case
	file(GLOB CSOURCES [a-z]*.c)

	# Define project Targets
	add_library(${TARGET_NAME} MODULE  ${CSOURCES})

	# Alsa Plugin properties
	SET_TARGET_PROPERTIES(${TARGET_NAME} PROPERTIES
			LABELS "PLUGIN"
			PREFIX ""
			SUFFIX ".ctlso"
			OUTPUT_NAME ${TARGET_NAME}
	)

	# compilation include path
	target_include_directories(${TARGET_NAME}
	PRIVATE "${CMAKE_SOURCE_DIR}/src")
