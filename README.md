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

### Windows

Remember to place you .mhr files in here:

`%appdata%\openal\hrtf`

### Ubuntu and MacOS

They should go in these directories:

```
$XDG_DATA_HOME/openal/hrtf  (defaults to $HOME/.local/share/openal/hrtf)
$XDG_DATA_DIRS/openal/hrtf  (defaults to /usr/local/share/openal/hrtf and
                             /usr/share/openal/hrtf)
```

The above directories are checked in order for .mhr file presence
