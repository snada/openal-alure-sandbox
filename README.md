# OpenAL Soft and Alure

Base project to compile a C++ program with OpenAL-Soft and Alure support.

Alure 1.2 needs a small edit in it's CMakeLists file:

`SET(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")`

## Build

 `mkdir build`

 `cd build`

 `cmake ..`

 `make`

 ## Run

`./main`
