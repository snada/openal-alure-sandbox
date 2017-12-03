
MESSAGE(STATUS "${PROJECT_BINARY_DIR}")

INCLUDE(ExternalProject)

CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

PROJECT(Base)

SET(CMAKE_BUILD_TYPE Release)

SET(CMAKE_CXX_STANDARD 11)

SET(OPENAL_BINARY_DIR "${PROJECT_BINARY_DIR}/openal")
SET(ALURE_BINARY_DIR "${PROJECT_BINARY_DIR}/alure")

LINK_DIRECTORIES(
  "${OPENAL_BINARY_DIR}"
  "${ALURE_BINARY_DIR}"
)

ExternalProject_Add(
  OpenAL
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/dist/openal-soft-1.18.2"
  BINARY_DIR "${OPENAL_BINARY_DIR}"
  UPDATE_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  alure
  DEPENDS OpenAL
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/dist/alure"
  BINARY_DIR "${ALURE_BINARY_DIR}"
  UPDATE_COMMAND ""
  INSTALL_COMMAND ""
)

IF(WIN32)
  ADD_CUSTOM_COMMAND(TARGET OpenAL POST_BUILD        # Adds a post-build event to MyTest
      COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
          "${OPENAL_BINARY_DIR}/Debug/OpenAL32.dll"      # <--this is in-file
          "${PROJECT_BINARY_DIR}/Debug")                 # <--this is out-file path

  ADD_CUSTOM_COMMAND(TARGET alure POST_BUILD        # Adds a post-build event to MyTest
      COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
          "${ALURE_BINARY_DIR}/Debug/alure2.dll"      # <--this is in-file
          "${PROJECT_BINARY_DIR}/Debug")                 # <--this is out-file path
ENDIF()

INCLUDE_DIRECTORIES(
  "${PROJECT_SOURCE_DIR}/dist/openal-soft-1.18.2/include"
  "${PROJECT_SOURCE_DIR}/dist/alure/include"
)

ADD_EXECUTABLE(main main.cpp)
ADD_DEPENDENCIES(main OpenAL alure)

IF(WIN32)
  TARGET_LINK_LIBRARIES(main OpenAL32.lib alure2)
ELSE()
  TARGET_LINK_LIBRARIES(main openal alure2)
ENDIF()
