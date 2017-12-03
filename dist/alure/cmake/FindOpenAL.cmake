GET_FILENAME_COMPONENT(PARENT_BINARY_DIR ${PROJECT_BINARY_DIR} DIRECTORY)
GET_FILENAME_COMPONENT(PARENT_SOURCE_DIR ${PROJECT_SOURCE_DIR} DIRECTORY)

IF(WIN32)
  SET(OPENAL_LIBRARY "${PARENT_BINARY_DIR}/openal/Debug/OpenAL32.lib")
ELSE()
  SET(OPENAL_LIBRARY "${PARENT_BINARY_DIR}/openal/libopenal.so")
ENDIF()

SET(OPENAL_INCLUDE_DIR "${PARENT_SOURCE_DIR}/openal-soft-1.18.2/include/AL")

SET(OPENAL_FOUND TRUE)
