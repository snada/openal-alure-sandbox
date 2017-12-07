# OpenAL Soft and Alure Sandbox

Basic project template providing a full cross-platform local environment to develop audio applications with [OpenAL-Soft](http://kcat.strangesoft.net/openal.html) and [Alure](http://kcat.strangesoft.net/alure.html) by Chris "kcat" Robinson.

This work in progress project aims to quickly resolve the building issues on different platforms, providing easy access to awesome features like HRTF.

To achieve this, this project will be updated together with alure version 2 development, to expose all those features (like hrtf) not readily available on the 1.2 release.

This environment will:

- Compile a local build of the two libraries
- Link your executable against this local version
- Copy hrtf file definitions into the executable directory

This environment will produce a binary file only runnable within your machine (at least on Mac and Linux, as Windows will automatically search for shared libs on the exe directory). If you ever want to distribute your app, either install the dependencies on your system and package accordingly, or find your way to build a relocatable package via RPATH.

## Build

Clone this project, then:

 `mkdir build`

 `cd build`

 `cmake ..`

 `cmake --build .`

 This project expects you to use Visual Studio as build generator on Windows.

## Run

You will find your compiled binary inside your build directory.

### Linux problems opening default device

If you run the executable and you get an error like:

```
Could not open /dev/dsp
```

Try installing `alsa-oss` package and run your example like this:

`aoss ./main /path/to/file.wav
