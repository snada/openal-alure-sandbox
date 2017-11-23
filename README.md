# OpenAL Soft and Alure

Base project to compile a C++ program with OpenAL-Soft and Alure support.

## Build

 `mkdir build`

 `cd build`

 `cmake ..`

 `make`

 ## Run

On Linux it should run on the fly: if your default device is OSS, then you should also install alsa-oss package to have it compatible.

The run:

`aoss ./main -hrtf default-48000 /path/to/file.wav`

On MacOS, it should run (if I ever manage to have hrtf libs found on the system...!)
