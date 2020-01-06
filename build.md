# Building CNQ3

## Toolchains

There are 3 supported build toolchains:

- Visual C++ on Windows x64 and x86
- GCC on Linux x64
- GCC on FreeBSD x64

## Directories

| Directory | Contains                                                      |
|:----------|:--------------------------------------------------------------|
| makefiles | premake script and pre-generated Visual C++ and GNU makefiles |
| .build    | intermediate build files                                      |
| .bin      | final executables (and symbol database files on Windows)      |

## Building on Windows with Visual C++ 2013 or later

**Requirements**

- The %QUAKE3DIR% environment variable must be defined to the absolute path to copy the .exe and .pdb files to
- NASM.exe is in your path for building the client

**Options**

- The %CPMADIR% environment variable must be defined to the name of the mod directory for launching through the debugger

**Build steps**

- Open makefiles\windows_vs2013\cnq3.sln
- Build

**Notes**

- You don't need to set environment variables globally
- Instead, we recomment you set them for the Visual Studio process only

Here's an example batch script for opening the Visual Studio solution:
```
cd cnq3\makefiles\windows_vs2013
set QUAKE3DIR=G:\CPMA_tests
set CPMADIR=cpma_dev
cnq3.sln
```
With this set-up, you can press F5 and run the engine on the right q3 install and right mod folder immediately.

## Building on Linux / FreeBSD with GCC

**Requirements**

| Name  | Server | Client | Debian package | FreeBSD package |
|:------|:------:|:------:|:---------------|:----------------|
| NASM  | X      | X      | nasm           | nasm            |
| SDL 2 |        | X      | libsdl2-dev    | sdl2            |

 On FreeBSD, we link against libexecinfo for the backtrace functions. We thus require FreeBSD 10.0 as a minimum target.

**Options**

- The $(QUAKE3DIR) environment variable can define the absolute path to copy the executables to

**Build steps**

- Navigate to the root of the repository
- Run `make [config=debug|release] all|client|server` to build on Linux  
  For FreeBSD, use `gmake` instead of `make`

**Notes**

- To create the QUAKE3DIR variable in the build shell, you can use `export QUAKE3DIR=~/games/q3`.
- To delete the variable from the build shell, you can use `unset QUAKE3DIR`.

## Environment variables

There are 2 environment variables used for compiling and debugging:

| Env. Var. | Meaning                 | Example     |
|:----------|:------------------------|:------------|
| QUAKE3DIR | absolute directory path | C:\Games\Q3 |
| CPMADIR   | mod folder name         | cpma        |

| Env. Var. | Windows                | Linux / FreeBSD |
|:----------|:-----------------------|:----------------|
| QUAKE3DIR | required for building  | optional        |
| CPMADIR   | required for debugging | unused          |

## Building with other compilers

While it's not officially supported, you can modify the premake Lua script and run premake on it to generate new makefiles for your own needs.

## Bonus: Building SDL 2 from source on Linux

- Download the sources of the latest stable release
- Extract to sdl-src
- Create sdl-build next to sdl-src (it *has* to be out-of-tree)
- Navigate to the sdl-build directory with `cd`
- Run `../sdl-src/configure`
- Run `make`
- As superuser, run `make install` (or `sudo make install`)
- Run `ldconfig` to update the library paths
