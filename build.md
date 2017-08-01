Toolchains
----------

There are only 2 build toolchains supported:

- Visual C++ on Windows
- GCC on Linux

Directories
-----------

| Directory | Contains |
|-----------|----------|
| makefiles | premake script and pre-generated Visual C++ and GNU makefiles |
| .build    | intermediate build files |
| .bin      | final executables (and symbol database files on Windows) |

Building on Windows with Visual C++ 2013 or later
-------------------------------------------------

Requirements:

- The %QUAKE3DIR% environment variable must be defined to the absolute path to copy the .exe and .pdb files to
- NASM.exe is in your path for building the client

Options:

- The %CPMADIR% environment variable must be defined to the name of the mod directory for launching through the debugger

Build steps:

- Open makefiles\vs2013\cnq3.sln
- Build

Notes:
You don't need to set environment variables globally.  
Instead, we recomment you set them for the Visual Studio process only.  
Here's an example batch script for opening the Visual Studio solution:
```
cd cnq3\makefiles\vs2013
set QUAKE3DIR=G:\CPMA_tests
set CPMADIR=cpma_dev
cnq3.sln
```
With this set-up, you can press F5 and run the engine on the right q3 install and right mod folder immediately.

Building on Linux with GCC
--------------------------

Requirements:

- NASM | On Debian and Ubuntu, run `sudo apt-get install nasm` to install it

Options:

- The $(QUAKE3DIR) environment variable can define the absolute path to copy the executables to

Build steps:

- Navigate to the root of the repository
- Run `make all|client|server` to build

Notes:
To create the QUAKE3DIR variable in the build shell, you can use `export QUAKE3DIR=~/games/q3`.  
To delete the variable from the build shell, you can use `unset QUAKE3DIR`.

Environment variables
---------------------

There are 2 environment variables used for compiling and debugging:

| Env. Var. | Meaning | Example |
|-----------| --------|---------|
| QUAKE3DIR | absolute directory path | C:\Games\Q3 |
| CPMADIR   | mod folder name         | cpma |

|           | Windows                | Linux    |
|-----------|------------------------|----------|
| QUAKE3DIR | required for building  | optional |
| CPMADIR   | required for debugging | unused   |

Build with other compilers
--------------------------

While it's not officially supported, you can modify the premake Lua script and run premake on it to generate new makefiles for your own needs.
