#
# Quake3 Unix Makefile
#
# GNU Make required
#

COMPILE_PLATFORM=$(shell uname|sed -e s/_.*//|tr '[:upper:]' '[:lower:]'|sed -e 's/\//_/g')

COMPILE_ARCH=$(shell uname -m | sed -e s/i.86/i386/)

ifeq ($(COMPILE_PLATFORM),sunos)
  # Solaris uname and GNU uname differ
  COMPILE_ARCH=$(shell uname -p | sed -e s/i.86/i386/)
endif
ifeq ($(COMPILE_PLATFORM),darwin)
  # Apple does some things a little differently...
  COMPILE_ARCH=$(shell uname -p | sed -e s/i.86/i386/)
endif

ifeq ($(COMPILE_PLATFORM),mingw32)
  ifeq ($(COMPILE_ARCH),i386)
    COMPILE_ARCH=x86
  endif
endif

BUILD_SERVER     =
BUILD_CLIENT     =

# needs stl, cbf to fight with gcc
BUILD_TOOLS = 0

# this should probably be deleted/merged anyway
BUILD_CLIENT_SMP = 0


#############################################################################
#
# If you require a different configuration from the defaults below, create a
# new file named "Makefile.local" in the same directory as this file and define
# your parameters there. This allows you to change configuration without
# causing problems with keeping up to date with the repository.
#
#############################################################################
-include Makefile.local

ifndef PLATFORM
PLATFORM=$(COMPILE_PLATFORM)
endif
export PLATFORM

ifndef ARCH
ARCH=$(COMPILE_ARCH)
endif

ifeq ($(ARCH),powerpc)
  ARCH=ppc
endif
export ARCH

ifneq ($(PLATFORM),$(COMPILE_PLATFORM))
  CROSS_COMPILING=1
else
  CROSS_COMPILING=0

  ifneq ($(ARCH),$(COMPILE_ARCH))
    CROSS_COMPILING=1
  endif
endif
export CROSS_COMPILING

ifndef COPYDIR
COPYDIR="/usr/local/games/quake3"
endif

ifndef COPYBINDIR
COPYBINDIR=$(COPYDIR)
endif

ifndef MOUNT_DIR
MOUNT_DIR=code
endif

ifndef BUILD_DIR
BUILD_DIR=build
endif

ifndef TEMPDIR
TEMPDIR=/tmp
endif

ifndef GENERATE_DEPENDENCIES
GENERATE_DEPENDENCIES=1
endif

ifndef USE_CCACHE
USE_CCACHE=0
endif
export USE_CCACHE

ifndef USE_SDL
USE_SDL=1
endif

ifndef USE_CURL
USE_CURL=0
endif

ifndef USE_CURL_DLOPEN
  ifeq ($(PLATFORM),mingw32)
    USE_CURL_DLOPEN=0
  else
    USE_CURL_DLOPEN=1
  endif
endif

ifndef USE_CODEC_VORBIS
USE_CODEC_VORBIS=0
endif

#############################################################################

BD=$(BUILD_DIR)/debug-$(PLATFORM)-$(ARCH)
BR=$(BUILD_DIR)/release-$(PLATFORM)-$(ARCH)
CDIR=$(MOUNT_DIR)/client
SDIR=$(MOUNT_DIR)/server
RDIR=$(MOUNT_DIR)/renderer
CMDIR=$(MOUNT_DIR)/qcommon
UDIR=$(MOUNT_DIR)/unix
W32DIR=$(MOUNT_DIR)/win32
GDIR=$(MOUNT_DIR)/game
CGDIR=$(MOUNT_DIR)/cgame
BLIBDIR=$(MOUNT_DIR)/botlib
NDIR=$(MOUNT_DIR)/null
UIDIR=$(MOUNT_DIR)/ui
Q3UIDIR=$(MOUNT_DIR)/q3_ui
JPDIR=$(MOUNT_DIR)/jpeg-6
TOOLSDIR=$(MOUNT_DIR)/tools
LOKISETUPDIR=$(UDIR)/setup
SDLHDIR=$(MOUNT_DIR)/SDL12
LIBSDIR=$(MOUNT_DIR)/libs

# extract version info
VERSION=$(shell grep Q3_VERSION $(CMDIR)/q_shared.h | \
  sed -e 's/.*".* \([^ ]*\)"/\1/')

ifeq ($(wildcard .svn),.svn)
  SVN_VERSION=$(VERSION)_SVN$(shell LANG=C svnversion .)
else
  SVN_VERSION=$(VERSION)
endif


#############################################################################
# SETUP AND BUILD -- LINUX
#############################################################################

## Defaults
VM_PPC=

LIB=lib

INSTALL=install
MKDIR=mkdir

ifeq ($(PLATFORM),linux)

  CC=g++

  ifeq ($(ARCH),alpha)
    ARCH=axp
  else
  ifeq ($(ARCH),x86_64)
    ARCH=i386
  else
  ifeq ($(ARCH),ppc64)
    LIB=lib64
  else
  ifeq ($(ARCH),s390x)
    LIB=lib64
  endif
  endif
  endif
  endif

  BASE_CFLAGS = -Wall -Weffc++ -fno-operator-names -fno-strict-aliasing -Wimplicit -pipe
	BASE_CFLAGS += $(shell freetype-config --cflags)
	CLIENT_LDFLAGS += -lfreetype $(shell freetype-config --libs)

  ifeq ($(USE_CURL),1)
    BASE_CFLAGS += -DUSE_CURL=1
    ifeq ($(USE_CURL_DLOPEN),1)
      BASE_CFLAGS += -DUSE_CURL_DLOPEN=1
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    BASE_CFLAGS += -DUSE_CODEC_VORBIS=1
  endif

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += -DUSE_SDL_VIDEO=1 -DUSE_SDL_SOUND=1 $(shell sdl-config --cflags)
    GL_CFLAGS =
  else
    GL_CFLAGS = -I/usr/X11R6/include
  endif

  OPTIMIZE = -O2 -ffast-math -funroll-loops -fomit-frame-pointer 

  ifeq ($(ARCH),x86_64)
    OPTIMIZE = -O2 -fomit-frame-pointer -ffast-math -funroll-loops \
      -falign-loops=2 -falign-jumps=2 -falign-functions=2 \
      -fstrength-reduce
    # experimental x86_64 jit compiler! you need GNU as
    HAVE_VM_COMPILED = true
  else
  ifeq ($(ARCH),i386)
    OPTIMIZE = -O2 -march=i586 -fomit-frame-pointer -ffast-math \
      -funroll-loops -falign-loops=2 -falign-jumps=2 \
      -falign-functions=2 -fstrength-reduce
    HAVE_VM_COMPILED=true
  else
  ifeq ($(ARCH),ppc)
    BASE_CFLAGS += -maltivec
    ifneq ($(VM_PPC),)
      HAVE_VM_COMPILED=true
    endif
  endif
  endif
  endif

  # -fomit-frame-pointer on g++ causes crashes, ty Timbo
  OPTIMIZE = -O2 -march=i586 -ffast-math -funroll-loops 

  ifneq ($(HAVE_VM_COMPILED),true)
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -g -O0 

  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  THREAD_LDFLAGS=-lpthread
  LDFLAGS=-ldl -lm -lGL

  ifeq ($(USE_SDL),1)
    CLIENT_LDFLAGS += $(shell sdl-config --libs)
  else
    CLIENT_LDFLAGS += -L/usr/X11R6/$(LIB) -lX11 -lXext -lXxf86dga -lXxf86vm
  endif

  ifeq ($(USE_CURL),1)
    ifneq ($(USE_CURL_DLOPEN),1)
      CLIENT_LDFLAGS += -lcurl
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif

  ifeq ($(ARCH),i386)
    # linux32 make ...
    BASE_CFLAGS += -m32
    LDFLAGS+=-m32
  endif

else # ifeq Linux

#############################################################################
# SETUP AND BUILD -- MAC OS X
#############################################################################

ifeq ($(PLATFORM),darwin)
  CC=gcc

  # !!! FIXME: calling conventions are still broken! See Bugzilla #2519
  VM_PPC=vm_ppc_new

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes
  BASE_CFLAGS += -DMACOS_X -fno-common -pipe

  # Always include debug symbols...you can strip the binary later...
  BASE_CFLAGS += -gfull

  ifeq ($(USE_CURL),1)
    BASE_CFLAGS += -DUSE_CURL=1
    ifneq ($(USE_CURL_DLOPEN),1)
      CLIENT_LDFLAGS += -lcurl
    else
      BASE_CFLAGS += -DUSE_CURL_DLOPEN=1
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    BASE_CFLAGS += -DUSE_CODEC_VORBIS=1
  endif

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += -DUSE_SDL_VIDEO=1 -DUSE_SDL_SOUND=1 -D_THREAD_SAFE=1 -I$(SDLHDIR)/include
    GL_CFLAGS =
  endif

  OPTIMIZE = -O2 -ffast-math -falign-loops=16

  ifeq ($(ARCH),ppc)
  BASE_CFLAGS += -faltivec
    ifneq ($(VM_PPC),)
      HAVE_VM_COMPILED=true
    endif
  endif

  ifeq ($(ARCH),i386)
    # !!! FIXME: x86-specific flags here...
  endif

  ifneq ($(HAVE_VM_COMPILED),true)
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -g -O0

  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  SHLIBEXT=dylib
  SHLIBCFLAGS=-fPIC -fno-common
  SHLIBLDFLAGS=-dynamiclib $(LDFLAGS)

  NOTSHLIBCFLAGS=-mdynamic-no-pic

  #THREAD_LDFLAGS=-lpthread
  #LDFLAGS=-ldl -lm
  LDFLAGS += -framework Carbon

  ifeq ($(USE_SDL),1)
    # We copy sdlmain before ranlib'ing it so that subversion doesn't think
    #  the file has been modified by each build.
    LIBSDLMAIN=$(B)/libSDLmain.a
    LIBSDLMAINSRC=$(LIBSDIR)/macosx/libSDLmain.a
    CLIENT_LDFLAGS=-framework Cocoa -framework OpenGL $(LIBSDIR)/macosx/libSDL-1.2.0.dylib
  else
    # !!! FIXME: frameworks: OpenGL, Carbon, etc...
    #CLIENT_LDFLAGS=-L/usr/X11R6/$(LIB) -lX11 -lXext -lXxf86dga -lXxf86vm
  endif

  # -framework OpenAL requires 10.4 or later...for builds shipping to the
  #  public, you'll want to use USE_OPENAL_DLOPEN and ship your own OpenAL
  #  library (http://openal.org/ or http://icculus.org/al_osx/)
  ifeq ($(USE_OPENAL),1)
    ifneq ($(USE_OPENAL_DLOPEN),1)
      CLIENT_LDFLAGS += -framework OpenAL
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif

else # ifeq darwin


#############################################################################
# SETUP AND BUILD -- MINGW32
#############################################################################

ifeq ($(PLATFORM),mingw32)

  CC=gcc
  WINDRES=windres

  ARCH=x86

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes

  ifeq ($(USE_CURL),1)
    BASE_CFLAGS += -DUSE_CURL=1
    ifneq ($(USE_CURL_DLOPEN),1)
      BASE_CFLAGS += -DCURL_STATICLIB
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    BASE_CFLAGS += -DUSE_CODEC_VORBIS=1
  endif

  GL_CFLAGS =
  MINGW_CFLAGS = -DDONT_TYPEDEF_INT32

  OPTIMIZE = -O2 -march=i586 -fomit-frame-pointer -ffast-math -falign-loops=2 \
    -funroll-loops -falign-jumps=2 -falign-functions=2 -fstrength-reduce

  HAVE_VM_COMPILED = true

  DEBUG_CFLAGS=$(BASE_CFLAGS) -g -O0

  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  SHLIBEXT=dll
  SHLIBCFLAGS=
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  BINEXT=.exe

  LDFLAGS= -mwindows -lwsock32 -lgdi32 -lwinmm -lole32
  CLIENT_LDFLAGS=

  ifeq ($(USE_CURL),1)
    ifneq ($(USE_CURL_DLOPEN),1)
      CLIENT_LDFLAGS += $(LIBSDIR)/win32/libcurl.a
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif

  ifeq ($(ARCH),x86)
    # build 32bit
    BASE_CFLAGS += -m32
    LDFLAGS+=-m32
  endif

  BUILD_SERVER = 0
  BUILD_CLIENT_SMP = 0

else # ifeq mingw32

#############################################################################
# SETUP AND BUILD -- FREEBSD
#############################################################################

ifeq ($(PLATFORM),freebsd)

  ifneq (,$(findstring alpha,$(shell uname -m)))
    ARCH=axp
  else #default to i386
    ARCH=i386
  endif #alpha test


  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes

  GL_CFLAGS = -I/usr/X11R6/include

  DEBUG_CFLAGS=$(BASE_CFLAGS) -g

  ifeq ($(USE_OPENAL),1)
    BASE_CFLAGS += -DUSE_OPENAL=1
    ifeq ($(USE_OPENAL_DLOPEN),1)
      BASE_CFLAGS += -DUSE_OPENAL_DLOPEN=1
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    BASE_CFLAGS += -DUSE_CODEC_VORBIS=1
  endif

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += $(shell sdl-config --cflags) -DUSE_SDL_VIDEO=1 -DUSE_SDL_SOUND=1
  endif

  ifeq ($(ARCH),axp)
    CC=gcc
    BASE_CFLAGS += -DNO_VM_COMPILED
    RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O2 -ffast-math -funroll-loops \
      -fomit-frame-pointer -fexpensive-optimizations
  else
  ifeq ($(ARCH),i386)
    CC=gcc
    RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O2 -mtune=pentiumpro \
      -march=pentium -fomit-frame-pointer -pipe -ffast-math \
      -falign-loops=2 -falign-jumps=2 -falign-functions=2 \
      -funroll-loops -fstrength-reduce
    HAVE_VM_COMPILED=true
  else
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif
  endif

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  THREAD_LDFLAGS=-lpthread
  # don't need -ldl (FreeBSD)
  LDFLAGS=-lm

  CLIENT_LDFLAGS =

  ifeq ($(USE_SDL),1)
    CLIENT_LDFLAGS += $(shell sdl-config --libs)
  else
    CLIENT_LDFLAGS += -L/usr/X11R6/$(LIB) -lGL -lX11 -lXext -lXxf86dga -lXxf86vm
  endif

  ifeq ($(USE_OPENAL),1)
    ifneq ($(USE_OPENAL_DLOPEN),1)
      CLIENT_LDFLAGS += $(THREAD_LDFLAGS) -lopenal
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif


else # ifeq freebsd
#############################################################################
# SETUP AND BUILD -- OPENBSD
#############################################################################

ifeq ($(PLATFORM),openbsd)

  #default to i386, no tests done on anything else
  ARCH=i386


  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
    -DUSE_ICON $(shell sdl-config --cflags)

  ifeq ($(USE_OPENAL),1)
    BASE_CFLAGS += -DUSE_OPENAL
    ifeq ($(USE_OPENAL_DLOPEN),1)
      BASE_CFLAGS += -DUSE_OPENAL_DLOPEN
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    BASE_CFLAGS += -DUSE_CODEC_VORBIS
  endif

  BASE_CFLAGS += -DNO_VM_COMPILED -I/usr/X11R6/include -I/usr/local/include
  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O3 \
    -march=pentium -fomit-frame-pointer -pipe -ffast-math \
    -falign-loops=2 -falign-jumps=2 -falign-functions=2 \
    -funroll-loops -fstrength-reduce
  HAVE_VM_COMPILED=false

  DEBUG_CFLAGS=$(BASE_CFLAGS) -g

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  THREAD_LDFLAGS=-lpthread
  LDFLAGS=-lm

  CLIENT_LDFLAGS =

  CLIENT_LDFLAGS += $(shell sdl-config --libs) -lGL

  ifeq ($(USE_OPENAL),1)
    ifneq ($(USE_OPENAL_DLOPEN),1)
      CLIENT_LDFLAGS += $(THREAD_LDFLAGS) -lopenal
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif


else # ifeq openbsd

#############################################################################
# SETUP AND BUILD -- NETBSD
#############################################################################

ifeq ($(PLATFORM),netbsd)

  ifeq ($(shell uname -m),i386)
    ARCH=i386
  endif

  CC=gcc
  LDFLAGS=-lm
  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)
  THREAD_LDFLAGS=-lpthread

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes
  DEBUG_CFLAGS=$(BASE_CFLAGS) -g

  ifneq ($(ARCH),i386)
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif

  BUILD_CLIENT = 0

else # ifeq netbsd

#############################################################################
# SETUP AND BUILD -- IRIX
#############################################################################

ifeq ($(PLATFORM),irix)

  ARCH=mips  #default to MIPS

  CC=cc
  BASE_CFLAGS=-Dstricmp=strcasecmp -Xcpluscomm -woff 1185 -mips3 \
    -nostdinc -I. -I$(ROOT)/usr/include -DNO_VM_COMPILED
  RELEASE_CFLAGS=$(BASE_CFLAGS) -O2
  DEBUG_CFLAGS=$(BASE_CFLAGS) -g

  SHLIBEXT=so
  SHLIBCFLAGS=
  SHLIBLDFLAGS=-shared

  LDFLAGS=-ldl -lm
  CLIENT_LDFLAGS=-L/usr/X11/$(LIB) -lGL -lX11 -lXext -lm

else # ifeq IRIX

#############################################################################
# SETUP AND BUILD -- SunOS
#############################################################################

ifeq ($(PLATFORM),sunos)

  CC=gcc
  INSTALL=ginstall
  MKDIR=gmkdir
  COPYDIR="/usr/local/share/games/quake3"

  ifneq (,$(findstring i86pc,$(shell uname -m)))
    ARCH=i386
  else #default to sparc
    ARCH=sparc
  endif

  ifneq ($(ARCH),i386)
    ifneq ($(ARCH),sparc)
      $(error arch $(ARCH) is currently not supported)
    endif
  endif


  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes -pipe

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += -DUSE_SDL_SOUND=1 $(shell sdl-config --cflags)
    GL_CFLAGS =
  else
    GL_CFLAGS = -I/usr/openwin/include
  endif

  OPTIMIZE = -O2 -ffast-math -funroll-loops

  ifeq ($(ARCH),sparc)
    OPTIMIZE = -O2 -ffast-math -falign-loops=2 \
      -falign-jumps=2 -falign-functions=2 -fstrength-reduce \
      -mtune=ultrasparc -mv8plus -mno-faster-structs \
      -funroll-loops
    BASE_CFLAGS += -DNO_VM_COMPILED
  else
  ifeq ($(ARCH),i386)
    OPTIMIZE = -O2 -march=i586  -ffast-math \
      -falign-loops=2 -falign-jumps=2 -falign-functions=2 \
      -funroll-loops -fstrength-reduce
  endif
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -ggdb -O0

  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  THREAD_LDFLAGS=-lpthread
  LDFLAGS=-lsocket -lnsl -ldl -lm

  BOTCFLAGS=-O0

  ifeq ($(USE_SDL),1)
    CLIENT_LDFLAGS=$(shell sdl-config --libs) -L/usr/X11/lib -lGLU -lX11 -lXext
  else
    CLIENT_LDFLAGS=-L/usr/openwin/$(LIB) -L/usr/X11/lib -lGLU -lX11 -lXext
  endif

  ifeq ($(ARCH),i386)
    # Solarix x86 make ...
    BASE_CFLAGS += -m32
    LDFLAGS+=-m32
  endif

else # ifeq sunos

#############################################################################
# SETUP AND BUILD -- GENERIC
#############################################################################
  CC=cc
  BASE_CFLAGS=-DNO_VM_COMPILED
  DEBUG_CFLAGS=$(BASE_CFLAGS) -g
  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O2

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared

endif #Linux
endif #darwin
endif #mingw32
endif #FreeBSD
endif #OpenBSD
endif #NetBSD
endif #IRIX
endif #SunOS

TARGETS =

ifneq ($(BUILD_SERVER),0)
  TARGETS += $(B)/cnq3ded.$(ARCH)$(BINEXT)
endif

ifneq ($(BUILD_CLIENT),0)
  TARGETS += $(B)/cnq3.$(ARCH)$(BINEXT)
endif

ifneq ($(BUILD_TOOLS),0)
  TARGETS += tools
endif

ifeq ($(USE_CCACHE),1)
  CC := ccache $(CC)
endif

ifdef DEFAULT_BASEDIR
  BASE_CFLAGS += -DDEFAULT_BASEDIR=\\\"$(DEFAULT_BASEDIR)\\\"
endif

ifeq ($(GENERATE_DEPENDENCIES),1)
  ifeq ($(CC),gcc)
    DEPEND_CFLAGS=-MMD
  endif
endif

DO_CC=$(CC) $(NOTSHLIBCFLAGS) $(CFLAGS) -o $@ -c $<
DO_SMP_CC=$(CC) $(NOTSHLIBCFLAGS) $(CFLAGS) -DSMP -o $@ -c $<
DO_BOT_CC=$(CC) $(NOTSHLIBCFLAGS) $(CFLAGS) $(BOTCFLAGS) -DBOTLIB -o $@ -c $<   # $(SHLIBCFLAGS) # bk001212
DO_DEBUG_CC=$(CC) $(NOTSHLIBCFLAGS) $(DEBUG_CFLAGS) -o $@ -c $<
DO_SHLIB_CC=$(CC) $(CFLAGS) $(SHLIBCFLAGS) -o $@ -c $<
DO_SHLIB_DEBUG_CC=$(CC) $(DEBUG_CFLAGS) $(SHLIBCFLAGS) -o $@ -c $<
DO_AS=$(CC) $(CFLAGS) -DELF -x assembler-with-cpp -o $@ -c $<
DO_DED_CC=$(CC) $(NOTSHLIBCFLAGS) -DDEDICATED $(CFLAGS) -o $@ -c $<
DO_WINDRES=$(WINDRES) -i $< -o $@

#############################################################################
# MAIN TARGETS
#############################################################################

default:build_release

debug: build_debug
release: build_release

build_debug: B=$(BD)
build_debug: makedirs
	$(MAKE)  targets B=$(BD) CFLAGS="$(CFLAGS) $(DEBUG_CFLAGS) $(DEPEND_CFLAGS)"

build_release: B=$(BR)
build_release: makedirs
	$(MAKE)  targets B=$(BR) CFLAGS="$(CFLAGS) $(RELEASE_CFLAGS) $(DEPEND_CFLAGS)"

#Build both debug and release builds
all:build_debug build_release

targets: $(TARGETS)

makedirs:
	@if [ ! -d $(BUILD_DIR) ];then $(MKDIR) $(BUILD_DIR);fi
	@if [ ! -d $(B) ];then $(MKDIR) $(B);fi
	@if [ ! -d $(B)/client ];then $(MKDIR) $(B)/client;fi
	@if [ ! -d $(B)/ded ];then $(MKDIR) $(B)/ded;fi

#############################################################################
# QVM BUILD TOOLS
#############################################################################

Q3LCC=$(TOOLSDIR)/q3lcc$(BINEXT)
Q3ASM=$(TOOLSDIR)/q3asm$(BINEXT)

ifneq ($(BUILD_TOOLS),0)
  ifeq ($(CROSS_COMPILING),1)
  tools:
    echo QVM tools not built when cross-compiling
  else
  tools:
    $(MAKE) -C $(TOOLSDIR)/lcc install
    $(MAKE) -C $(TOOLSDIR)/asm install
  endif
endif

DO_Q3LCC=$(Q3LCC) -o $@ $<

#############################################################################
# CLIENT/SERVER
#############################################################################

Q3OBJ = \
  $(B)/client/cl_browser.o \
  $(B)/client/cl_cgame.o \
  $(B)/client/cl_cin.o \
  $(B)/client/cl_console.o \
  $(B)/client/cl_input.o \
  $(B)/client/cl_keys.o \
  $(B)/client/cl_main.o \
  $(B)/client/cl_net_chan.o \
  $(B)/client/cl_parse.o \
  $(B)/client/cl_scrn.o \
  $(B)/client/cl_ui.o \
  $(B)/client/cl_avi.o \
  \
  $(B)/client/cm_load.o \
  $(B)/client/cm_patch.o \
  $(B)/client/cm_polylib.o \
  $(B)/client/cm_test.o \
  $(B)/client/cm_trace.o \
  \
  $(B)/client/cmd.o \
  $(B)/client/common.o \
  $(B)/client/cvar.o \
  $(B)/client/files.o \
  $(B)/client/md4.o \
  $(B)/client/md5.o \
  $(B)/client/msg.o \
  $(B)/client/net_chan.o \
  $(B)/client/net_ip.o \
  $(B)/client/huffman.o \
  \
  $(B)/client/snd_dma.o \
  $(B)/client/snd_mem.o \
  $(B)/client/snd_mix.o \
  \
  $(B)/client/snd_main.o \
  $(B)/client/snd_codec.o \
  $(B)/client/snd_codec_wav.o \
  $(B)/client/snd_codec_ogg.o \
  \
  $(B)/client/cl_curl.o \
  \
  $(B)/client/sv_bot.o \
  $(B)/client/sv_ccmds.o \
  $(B)/client/sv_client.o \
  $(B)/client/sv_game.o \
  $(B)/client/sv_init.o \
  $(B)/client/sv_main.o \
  $(B)/client/sv_net_chan.o \
  $(B)/client/sv_snapshot.o \
  $(B)/client/sv_world.o \
  \
  $(B)/client/q_math.o \
  $(B)/client/q_shared.o \
  \
  $(B)/client/unzip.o \
  $(B)/client/vm.o \
  $(B)/client/vm_interpreted.o \
  \
  $(B)/client/be_aas_bspq3.o \
  $(B)/client/be_aas_cluster.o \
  $(B)/client/be_aas_debug.o \
  $(B)/client/be_aas_entity.o \
  $(B)/client/be_aas_file.o \
  $(B)/client/be_aas_main.o \
  $(B)/client/be_aas_move.o \
  $(B)/client/be_aas_optimize.o \
  $(B)/client/be_aas_reach.o \
  $(B)/client/be_aas_route.o \
  $(B)/client/be_aas_routealt.o \
  $(B)/client/be_aas_sample.o \
  $(B)/client/be_ai_char.o \
  $(B)/client/be_ai_chat.o \
  $(B)/client/be_ai_gen.o \
  $(B)/client/be_ai_goal.o \
  $(B)/client/be_ai_move.o \
  $(B)/client/be_ai_weap.o \
  $(B)/client/be_ai_weight.o \
  $(B)/client/be_ea.o \
  $(B)/client/be_interface.o \
  $(B)/client/l_crc.o \
  $(B)/client/l_libvar.o \
  $(B)/client/l_log.o \
  $(B)/client/l_memory.o \
  $(B)/client/l_precomp.o \
  $(B)/client/l_script.o \
  $(B)/client/l_struct.o \
  \
  $(B)/client/jcapimin.o \
  $(B)/client/jchuff.o   \
  $(B)/client/jcinit.o \
  $(B)/client/jccoefct.o  \
  $(B)/client/jccolor.o \
  $(B)/client/jfdctflt.o \
  $(B)/client/jcdctmgr.o \
  $(B)/client/jcphuff.o \
  $(B)/client/jcmainct.o \
  $(B)/client/jcmarker.o \
  $(B)/client/jcmaster.o \
  $(B)/client/jcomapi.o \
  $(B)/client/jcparam.o \
  $(B)/client/jcprepct.o \
  $(B)/client/jcsample.o \
  $(B)/client/jdapimin.o \
  $(B)/client/jdapistd.o \
  $(B)/client/jdatasrc.o \
  $(B)/client/jdcoefct.o \
  $(B)/client/jdcolor.o \
  $(B)/client/jddctmgr.o \
  $(B)/client/jdhuff.o \
  $(B)/client/jdinput.o \
  $(B)/client/jdmainct.o \
  $(B)/client/jdmarker.o \
  $(B)/client/jdmaster.o \
  $(B)/client/jdpostct.o \
  $(B)/client/jdsample.o \
  $(B)/client/jdtrans.o \
  $(B)/client/jerror.o \
  $(B)/client/jidctflt.o \
  $(B)/client/jmemmgr.o \
  $(B)/client/jmemnobs.o \
  $(B)/client/jutils.o \
  \
  $(B)/client/tr_extensions.o \
  $(B)/client/tr_arb.o \
  $(B)/client/tr_backend.o \
  $(B)/client/tr_bsp.o \
  $(B)/client/tr_cmds.o \
  $(B)/client/tr_curve.o \
  $(B)/client/tr_flares.o \
  $(B)/client/tr_font.o \
  $(B)/client/tr_image.o \
  $(B)/client/tr_init.o \
  $(B)/client/tr_light.o \
  $(B)/client/tr_main.o \
  $(B)/client/tr_marks.o \
  $(B)/client/tr_mesh.o \
  $(B)/client/tr_model.o \
  $(B)/client/tr_noise.o \
  $(B)/client/tr_scene.o \
  $(B)/client/tr_shade.o \
  $(B)/client/tr_shade_calc.o \
  $(B)/client/tr_shader.o \
  $(B)/client/tr_sky.o \
  $(B)/client/tr_surface.o \
  $(B)/client/tr_world.o \

ifeq ($(ARCH),i386)
  Q3OBJ += \
    $(B)/client/matha.o \
    $(B)/client/ftola.o
endif
ifeq ($(ARCH),x86)
  Q3OBJ += \
    $(B)/client/matha.o \
    $(B)/client/ftola.o
endif

ifeq ($(HAVE_VM_COMPILED),true)
  ifeq ($(ARCH),i386)
    Q3OBJ += $(B)/client/vm_x86.o
  endif
  ifeq ($(ARCH),x86)
    Q3OBJ += $(B)/client/vm_x86.o
  endif
  ifeq ($(ARCH),ppc)
    Q3OBJ += $(B)/client/$(VM_PPC).o
  endif
endif

ifeq ($(PLATFORM),mingw32)
  Q3OBJ += \
    $(B)/client/win_gamma.o \
    $(B)/client/win_glimp.o \
    $(B)/client/win_input.o \
    $(B)/client/win_main.o \
    $(B)/client/win_net.o \
    $(B)/client/win_qgl.o \
    $(B)/client/win_shared.o \
    $(B)/client/win_snd.o \
    $(B)/client/win_syscon.o \
    $(B)/client/win_wndproc.o \
    $(B)/client/win_resource.o
else
  Q3OBJ += \
    $(B)/client/unix_main.o \
    $(B)/client/unix_shared.o \
    $(B)/client/linux_signals.o \
    $(B)/client/linux_qgl.o \
    $(B)/client/linux_snd.o \
    $(B)/client/sdl_snd.o

  ifeq ($(PLATFORM),linux)
    Q3OBJ += $(B)/client/linux_joystick.o
  endif

  BUILD_CLIENT_SMP = 0

  Q3POBJ = \
    $(B)/client/linux_glimp.o \
    $(B)/client/sdl_glimp.o

endif

$(B)/cnq3.$(ARCH)$(BINEXT): $(Q3OBJ) $(Q3POBJ) $(LIBSDLMAIN)
	$(CC)  -o $@ $(Q3OBJ) $(Q3POBJ) $(CLIENT_LDFLAGS) $(LDFLAGS) $(LIBSDLMAIN)


ifneq ($(strip $(LIBSDLMAIN)),)
ifneq ($(strip $(LIBSDLMAINSRC)),)
$(LIBSDLMAIN) : $(LIBSDLMAINSRC)
	cp $< $@
	ranlib $@
endif
endif

$(B)/client/cl_browser.o : $(CDIR)/cl_browser.cpp; $(DO_CC)
$(B)/client/cl_cgame.o : $(CDIR)/cl_cgame.cpp; $(DO_CC)
$(B)/client/cl_cin.o : $(CDIR)/cl_cin.cpp; $(DO_CC)
$(B)/client/cl_console.o : $(CDIR)/cl_console.cpp; $(DO_CC)
$(B)/client/cl_input.o : $(CDIR)/cl_input.cpp; $(DO_CC)
$(B)/client/cl_keys.o : $(CDIR)/cl_keys.cpp; $(DO_CC)
$(B)/client/cl_main.o : $(CDIR)/cl_main.cpp; $(DO_CC)
$(B)/client/cl_net_chan.o : $(CDIR)/cl_net_chan.cpp; $(DO_CC)
$(B)/client/cl_parse.o : $(CDIR)/cl_parse.cpp; $(DO_CC)
$(B)/client/cl_scrn.o : $(CDIR)/cl_scrn.cpp; $(DO_CC)
$(B)/client/cl_ui.o : $(CDIR)/cl_ui.cpp; $(DO_CC)
$(B)/client/cl_avi.o : $(CDIR)/cl_avi.cpp; $(DO_CC)
$(B)/client/snd_dma.o : $(CDIR)/snd_dma.cpp; $(DO_CC)
$(B)/client/snd_mem.o : $(CDIR)/snd_mem.cpp; $(DO_CC)
$(B)/client/snd_mix.o : $(CDIR)/snd_mix.cpp; $(DO_CC)

$(B)/client/snd_main.o : $(CDIR)/snd_main.cpp; $(DO_CC)
$(B)/client/snd_codec.o : $(CDIR)/snd_codec.cpp; $(DO_CC)
$(B)/client/snd_codec_wav.o : $(CDIR)/snd_codec_wav.cpp; $(DO_CC)
$(B)/client/snd_codec_ogg.o : $(CDIR)/snd_codec_ogg.cpp; $(DO_CC)

$(B)/client/cl_curl.o : $(CDIR)/cl_curl.cpp; $(DO_CC)

$(B)/client/sv_bot.o : $(SDIR)/sv_bot.cpp; $(DO_CC)
$(B)/client/sv_client.o : $(SDIR)/sv_client.cpp; $(DO_CC)
$(B)/client/sv_ccmds.o : $(SDIR)/sv_ccmds.cpp; $(DO_CC)
$(B)/client/sv_game.o : $(SDIR)/sv_game.cpp; $(DO_CC)
$(B)/client/sv_init.o : $(SDIR)/sv_init.cpp; $(DO_CC)
$(B)/client/sv_main.o : $(SDIR)/sv_main.cpp; $(DO_CC)
$(B)/client/sv_net_chan.o : $(SDIR)/sv_net_chan.cpp; $(DO_CC)
$(B)/client/sv_snapshot.o : $(SDIR)/sv_snapshot.cpp; $(DO_CC)
$(B)/client/sv_world.o : $(SDIR)/sv_world.cpp; $(DO_CC)
$(B)/client/cm_trace.o : $(CMDIR)/cm_trace.cpp; $(DO_CC)
$(B)/client/cm_load.o : $(CMDIR)/cm_load.cpp; $(DO_CC)
$(B)/client/cm_test.o : $(CMDIR)/cm_test.cpp; $(DO_CC)
$(B)/client/cm_patch.o : $(CMDIR)/cm_patch.cpp; $(DO_CC)
$(B)/client/cm_polylib.o : $(CMDIR)/cm_polylib.cpp; $(DO_CC)
$(B)/client/cmd.o : $(CMDIR)/cmd.cpp; $(DO_CC)
$(B)/client/common.o : $(CMDIR)/common.cpp; $(DO_CC)
$(B)/client/cvar.o : $(CMDIR)/cvar.cpp; $(DO_CC)
$(B)/client/files.o : $(CMDIR)/files.cpp; $(DO_CC)
$(B)/client/md4.o : $(CMDIR)/md4.cpp; $(DO_CC)
$(B)/client/md5.o : $(CMDIR)/md5.cpp; $(DO_CC)
$(B)/client/msg.o : $(CMDIR)/msg.cpp; $(DO_CC)
$(B)/client/net_chan.o : $(CMDIR)/net_chan.cpp; $(DO_CC)
$(B)/client/net_ip.o : $(CMDIR)/net_ip.cpp; $(DO_CC)
$(B)/client/huffman.o : $(CMDIR)/huffman.cpp; $(DO_CC)
$(B)/client/q_shared.o : $(CMDIR)/q_shared.c; $(DO_CC)
$(B)/client/q_math.o : $(CMDIR)/q_math.c; $(DO_CC)

$(B)/client/be_aas_bspq3.o : $(BLIBDIR)/be_aas_bspq3.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_cluster.o : $(BLIBDIR)/be_aas_cluster.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_debug.o : $(BLIBDIR)/be_aas_debug.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_entity.o : $(BLIBDIR)/be_aas_entity.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_file.o : $(BLIBDIR)/be_aas_file.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_main.o : $(BLIBDIR)/be_aas_main.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_move.o : $(BLIBDIR)/be_aas_move.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_optimize.o : $(BLIBDIR)/be_aas_optimize.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_reach.o : $(BLIBDIR)/be_aas_reach.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_route.o : $(BLIBDIR)/be_aas_route.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_routealt.o : $(BLIBDIR)/be_aas_routealt.cpp; $(DO_BOT_CC)
$(B)/client/be_aas_sample.o : $(BLIBDIR)/be_aas_sample.cpp; $(DO_BOT_CC)
$(B)/client/be_ai_char.o : $(BLIBDIR)/be_ai_char.cpp; $(DO_BOT_CC)
$(B)/client/be_ai_chat.o : $(BLIBDIR)/be_ai_chat.cpp; $(DO_BOT_CC)
$(B)/client/be_ai_gen.o : $(BLIBDIR)/be_ai_gen.cpp; $(DO_BOT_CC)
$(B)/client/be_ai_goal.o : $(BLIBDIR)/be_ai_goal.cpp; $(DO_BOT_CC)
$(B)/client/be_ai_move.o : $(BLIBDIR)/be_ai_move.cpp; $(DO_BOT_CC)
$(B)/client/be_ai_weap.o : $(BLIBDIR)/be_ai_weap.cpp; $(DO_BOT_CC)
$(B)/client/be_ai_weight.o : $(BLIBDIR)/be_ai_weight.cpp; $(DO_BOT_CC)
$(B)/client/be_ea.o : $(BLIBDIR)/be_ea.cpp; $(DO_BOT_CC)
$(B)/client/be_interface.o : $(BLIBDIR)/be_interface.cpp; $(DO_BOT_CC)
$(B)/client/l_crc.o : $(BLIBDIR)/l_crc.cpp; $(DO_BOT_CC)
$(B)/client/l_libvar.o : $(BLIBDIR)/l_libvar.cpp; $(DO_BOT_CC)
$(B)/client/l_log.o : $(BLIBDIR)/l_log.cpp; $(DO_BOT_CC)
$(B)/client/l_memory.o : $(BLIBDIR)/l_memory.cpp; $(DO_BOT_CC)
$(B)/client/l_precomp.o : $(BLIBDIR)/l_precomp.cpp; $(DO_BOT_CC)
$(B)/client/l_script.o : $(BLIBDIR)/l_script.cpp; $(DO_BOT_CC)
$(B)/client/l_struct.o : $(BLIBDIR)/l_struct.cpp; $(DO_BOT_CC)

$(B)/client/jcapimin.o : $(JPDIR)/jcapimin.c; $(DO_CC)
$(B)/client/jchuff.o : $(JPDIR)/jchuff.c; $(DO_CC)
$(B)/client/jcinit.o : $(JPDIR)/jcinit.c; $(DO_CC)
$(B)/client/jccoefct.o : $(JPDIR)/jccoefct.c; $(DO_CC)
$(B)/client/jccolor.o : $(JPDIR)/jccolor.c; $(DO_CC)
$(B)/client/jfdctflt.o : $(JPDIR)/jfdctflt.c; $(DO_CC)
$(B)/client/jcdctmgr.o : $(JPDIR)/jcdctmgr.c; $(DO_CC)
$(B)/client/jcmainct.o : $(JPDIR)/jcmainct.c; $(DO_CC)
$(B)/client/jcmarker.o : $(JPDIR)/jcmarker.c; $(DO_CC)
$(B)/client/jcmaster.o : $(JPDIR)/jcmaster.c; $(DO_CC)
$(B)/client/jcomapi.o : $(JPDIR)/jcomapi.c; $(DO_CC)
$(B)/client/jcparam.o : $(JPDIR)/jcparam.c;  $(DO_CC)
$(B)/client/jcprepct.o : $(JPDIR)/jcprepct.c; $(DO_CC)
$(B)/client/jcsample.o : $(JPDIR)/jcsample.c; $(DO_CC)

$(B)/client/jdapimin.o : $(JPDIR)/jdapimin.c; $(DO_CC)
$(B)/client/jdapistd.o : $(JPDIR)/jdapistd.c; $(DO_CC)
$(B)/client/jdatasrc.o : $(JPDIR)/jdatasrc.c; $(DO_CC)
$(B)/client/jdcoefct.o : $(JPDIR)/jdcoefct.c; $(DO_CC)
$(B)/client/jdcolor.o : $(JPDIR)/jdcolor.c; $(DO_CC)
$(B)/client/jcphuff.o : $(JPDIR)/jcphuff.c; $(DO_CC)
$(B)/client/jddctmgr.o : $(JPDIR)/jddctmgr.c; $(DO_CC)
$(B)/client/jdhuff.o : $(JPDIR)/jdhuff.c; $(DO_CC)
$(B)/client/jdinput.o : $(JPDIR)/jdinput.c; $(DO_CC)
$(B)/client/jdmainct.o : $(JPDIR)/jdmainct.c; $(DO_CC)
$(B)/client/jdmarker.o : $(JPDIR)/jdmarker.c; $(DO_CC)
$(B)/client/jdmaster.o : $(JPDIR)/jdmaster.c; $(DO_CC)
$(B)/client/jdpostct.o : $(JPDIR)/jdpostct.c; $(DO_CC)
$(B)/client/jdsample.o : $(JPDIR)/jdsample.c; $(DO_CC)
$(B)/client/jdtrans.o : $(JPDIR)/jdtrans.c; $(DO_CC)
$(B)/client/jerror.o : $(JPDIR)/jerror.c; $(DO_CC) $(GL_CFLAGS) $(MINGW_CFLAGS)
$(B)/client/jidctflt.o : $(JPDIR)/jidctflt.c; $(DO_CC)
$(B)/client/jmemmgr.o : $(JPDIR)/jmemmgr.c; $(DO_CC)
$(B)/client/jmemnobs.o : $(JPDIR)/jmemnobs.c; $(DO_CC)  $(GL_CFLAGS) $(MINGW_CFLAGS)
$(B)/client/jutils.o : $(JPDIR)/jutils.c; $(DO_CC)

$(B)/client/tr_extensions.o : $(RDIR)/tr_extensions.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_arb.o : $(RDIR)/tr_arb.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_bsp.o : $(RDIR)/tr_bsp.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_backend.o : $(RDIR)/tr_backend.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_cmds.o : $(RDIR)/tr_cmds.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_curve.o : $(RDIR)/tr_curve.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_flares.o : $(RDIR)/tr_flares.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_font.o : $(RDIR)/tr_font.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_image.o : $(RDIR)/tr_image.cpp; $(DO_CC)   $(GL_CFLAGS) $(MINGW_CFLAGS)
$(B)/client/tr_init.o : $(RDIR)/tr_init.cpp; $(DO_CC)    $(GL_CFLAGS)
$(B)/client/tr_light.o : $(RDIR)/tr_light.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_main.o : $(RDIR)/tr_main.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_marks.o : $(RDIR)/tr_marks.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_mesh.o : $(RDIR)/tr_mesh.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_model.o : $(RDIR)/tr_model.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_noise.o : $(RDIR)/tr_noise.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_scene.o : $(RDIR)/tr_scene.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_shade.o : $(RDIR)/tr_shade.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_shader.o : $(RDIR)/tr_shader.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_shade_calc.o : $(RDIR)/tr_shade_calc.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_sky.o : $(RDIR)/tr_sky.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_smp.o : $(RDIR)/tr_smp.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_stripify.o : $(RDIR)/tr_stripify.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_subdivide.o : $(RDIR)/tr_subdivide.cpp; $(DO_CC)   $(GL_CFLAGS)
$(B)/client/tr_surface.o : $(RDIR)/tr_surface.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/tr_world.o : $(RDIR)/tr_world.cpp; $(DO_CC)   $(GL_CFLAGS)

$(B)/client/unix_qgl.o : $(UDIR)/unix_qgl.cpp; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/unix_main.o : $(UDIR)/unix_main.cpp; $(DO_CC)
$(B)/client/unix_shared.o : $(UDIR)/unix_shared.cpp; $(DO_CC)
$(B)/client/irix_glimp.o : $(UDIR)/irix_glimp.cpp; $(DO_CC)
$(B)/client/irix_glimp_smp.o : $(UDIR)/irix_glimp.cpp; $(DO_SMP_CC)
$(B)/client/irix_snd.o : $(UDIR)/irix_snd.cpp; $(DO_CC)
$(B)/client/irix_input.o : $(UDIR)/irix_input.cpp; $(DO_CC)
$(B)/client/linux_signals.o : $(UDIR)/linux_signals.cpp; $(DO_CC) $(GL_CFLAGS)
$(B)/client/linux_glimp.o : $(UDIR)/linux_glimp.c; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/sdl_glimp.o : $(UDIR)/sdl_glimp.c; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/linux_joystick.o : $(UDIR)/linux_joystick.c; $(DO_CC)
$(B)/client/linux_qgl.o : $(UDIR)/linux_qgl.c; $(DO_CC)  $(GL_CFLAGS)
$(B)/client/linux_input.o : $(UDIR)/linux_input.cpp; $(DO_CC)
$(B)/client/linux_snd.o : $(UDIR)/linux_snd.c; $(DO_CC)
$(B)/client/sdl_snd.o : $(UDIR)/sdl_snd.c; $(DO_CC)
$(B)/client/matha.o : $(UDIR)/matha.s; $(DO_AS)
$(B)/client/ftola.o : $(UDIR)/ftola.s; $(DO_AS)

$(B)/client/win_gamma.o : $(W32DIR)/win_gamma.cpp; $(DO_CC)
$(B)/client/win_glimp.o : $(W32DIR)/win_glimp.cpp; $(DO_CC)
$(B)/client/win_input.o : $(W32DIR)/win_input.cpp; $(DO_CC)
$(B)/client/win_main.o : $(W32DIR)/win_main.cpp; $(DO_CC)
$(B)/client/win_net.o : $(W32DIR)/win_net.cpp; $(DO_CC)
$(B)/client/win_qgl.o : $(W32DIR)/win_qgl.cpp; $(DO_CC)
$(B)/client/win_shared.o : $(W32DIR)/win_shared.cpp; $(DO_CC)
$(B)/client/win_snd.o : $(W32DIR)/win_snd.cpp; $(DO_CC)
$(B)/client/win_syscon.o : $(W32DIR)/win_syscon.cpp; $(DO_CC)
$(B)/client/win_wndproc.o : $(W32DIR)/win_wndproc.cpp; $(DO_CC)
$(B)/client/win_resource.o : $(W32DIR)/winquake.rc; $(DO_WINDRES)

$(B)/client/vm_x86.o : $(CMDIR)/vm_x86.cpp; $(DO_CC)
ifneq ($(VM_PPC),)
$(B)/client/$(VM_PPC).o : $(CMDIR)/$(VM_PPC).cpp; $(DO_CC)
endif

$(B)/client/unzip.o : $(CMDIR)/unzip.cpp; $(DO_CC)
$(B)/client/vm.o : $(CMDIR)/vm.cpp; $(DO_CC)
$(B)/client/vm_interpreted.o : $(CMDIR)/vm_interpreted.cpp; $(DO_CC)

#############################################################################
# DEDICATED SERVER
#############################################################################

Q3DOBJ = \
  $(B)/ded/sv_bot.o \
  $(B)/ded/sv_client.o \
  $(B)/ded/sv_ccmds.o \
  $(B)/ded/sv_game.o \
  $(B)/ded/sv_init.o \
  $(B)/ded/sv_main.o \
  $(B)/ded/sv_net_chan.o \
  $(B)/ded/sv_snapshot.o \
  $(B)/ded/sv_world.o \
  \
  $(B)/ded/cm_load.o \
  $(B)/ded/cm_patch.o \
  $(B)/ded/cm_polylib.o \
  $(B)/ded/cm_test.o \
  $(B)/ded/cm_trace.o \
  $(B)/ded/cmd.o \
  $(B)/ded/common.o \
  $(B)/ded/cvar.o \
  $(B)/ded/files.o \
  $(B)/ded/md4.o \
  $(B)/ded/msg.o \
  $(B)/ded/net_chan.o \
  $(B)/ded/net_ip.o \
  $(B)/ded/huffman.o \
  \
  $(B)/ded/q_math.o \
  $(B)/ded/q_shared.o \
  \
  $(B)/ded/unzip.o \
  $(B)/ded/vm.o \
  $(B)/ded/vm_interpreted.o \
  \
  $(B)/ded/be_aas_bspq3.o \
  $(B)/ded/be_aas_cluster.o \
  $(B)/ded/be_aas_debug.o \
  $(B)/ded/be_aas_entity.o \
  $(B)/ded/be_aas_file.o \
  $(B)/ded/be_aas_main.o \
  $(B)/ded/be_aas_move.o \
  $(B)/ded/be_aas_optimize.o \
  $(B)/ded/be_aas_reach.o \
  $(B)/ded/be_aas_route.o \
  $(B)/ded/be_aas_routealt.o \
  $(B)/ded/be_aas_sample.o \
  $(B)/ded/be_ai_char.o \
  $(B)/ded/be_ai_chat.o \
  $(B)/ded/be_ai_gen.o \
  $(B)/ded/be_ai_goal.o \
  $(B)/ded/be_ai_move.o \
  $(B)/ded/be_ai_weap.o \
  $(B)/ded/be_ai_weight.o \
  $(B)/ded/be_ea.o \
  $(B)/ded/be_interface.o \
  $(B)/ded/l_crc.o \
  $(B)/ded/l_libvar.o \
  $(B)/ded/l_log.o \
  $(B)/ded/l_memory.o \
  $(B)/ded/l_precomp.o \
  $(B)/ded/l_script.o \
  $(B)/ded/l_struct.o \
  \
  $(B)/ded/linux_signals.o \
  $(B)/ded/unix_main.o \
  $(B)/ded/unix_shared.o \
  \
  $(B)/ded/null_client.o \
  $(B)/ded/null_input.o \
  $(B)/ded/null_snddma.o

ifeq ($(ARCH),i386)
  Q3DOBJ += \
      $(B)/ded/ftola.o \
      $(B)/ded/matha.o
endif

ifeq ($(HAVE_VM_COMPILED),true)
  ifeq ($(ARCH),i386)
    Q3DOBJ += $(B)/ded/vm_x86.o
  endif
  ifeq ($(ARCH),x86)
    Q3DOBJ += $(B)/ded/vm_x86.o
  endif
  ifeq ($(ARCH),ppc)
    Q3DOBJ += $(B)/ded/$(VM_PPC).o
  endif
endif

$(B)/cnq3ded.$(ARCH)$(BINEXT): $(Q3DOBJ)
	$(CC) -o $@ $(Q3DOBJ) $(LDFLAGS)

$(B)/ded/sv_bot.o : $(SDIR)/sv_bot.cpp; $(DO_DED_CC)
$(B)/ded/sv_client.o : $(SDIR)/sv_client.cpp; $(DO_DED_CC)
$(B)/ded/sv_ccmds.o : $(SDIR)/sv_ccmds.cpp; $(DO_DED_CC)
$(B)/ded/sv_game.o : $(SDIR)/sv_game.cpp; $(DO_DED_CC)
$(B)/ded/sv_init.o : $(SDIR)/sv_init.cpp; $(DO_DED_CC)
$(B)/ded/sv_main.o : $(SDIR)/sv_main.cpp; $(DO_DED_CC)
$(B)/ded/sv_net_chan.o : $(SDIR)/sv_net_chan.cpp; $(DO_DED_CC)
$(B)/ded/sv_snapshot.o : $(SDIR)/sv_snapshot.cpp; $(DO_DED_CC)
$(B)/ded/sv_world.o : $(SDIR)/sv_world.cpp; $(DO_DED_CC)
$(B)/ded/cm_load.o : $(CMDIR)/cm_load.cpp; $(DO_DED_CC)
$(B)/ded/cm_polylib.o : $(CMDIR)/cm_polylib.cpp; $(DO_DED_CC)
$(B)/ded/cm_test.o : $(CMDIR)/cm_test.cpp; $(DO_DED_CC)
$(B)/ded/cm_trace.o : $(CMDIR)/cm_trace.cpp; $(DO_DED_CC)
$(B)/ded/cm_patch.o : $(CMDIR)/cm_patch.cpp; $(DO_DED_CC)
$(B)/ded/cmd.o : $(CMDIR)/cmd.cpp; $(DO_DED_CC)
$(B)/ded/common.o : $(CMDIR)/common.cpp; $(DO_DED_CC)
$(B)/ded/cvar.o : $(CMDIR)/cvar.cpp; $(DO_DED_CC)
$(B)/ded/files.o : $(CMDIR)/files.cpp; $(DO_DED_CC)
$(B)/ded/md4.o : $(CMDIR)/md4.cpp; $(DO_DED_CC)
$(B)/ded/msg.o : $(CMDIR)/msg.cpp; $(DO_DED_CC)
$(B)/ded/net_chan.o : $(CMDIR)/net_chan.cpp; $(DO_DED_CC)
$(B)/ded/net_ip.o : $(CMDIR)/net_ip.cpp; $(DO_DED_CC)
$(B)/ded/huffman.o : $(CMDIR)/huffman.cpp; $(DO_DED_CC)
$(B)/ded/q_shared.o : $(CMDIR)/q_shared.c; $(DO_DED_CC)
$(B)/ded/q_math.o : $(CMDIR)/q_math.c; $(DO_DED_CC)

$(B)/ded/be_aas_bspq3.o : $(BLIBDIR)/be_aas_bspq3.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_cluster.o : $(BLIBDIR)/be_aas_cluster.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_debug.o : $(BLIBDIR)/be_aas_debug.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_entity.o : $(BLIBDIR)/be_aas_entity.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_file.o : $(BLIBDIR)/be_aas_file.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_main.o : $(BLIBDIR)/be_aas_main.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_move.o : $(BLIBDIR)/be_aas_move.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_optimize.o : $(BLIBDIR)/be_aas_optimize.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_reach.o : $(BLIBDIR)/be_aas_reach.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_route.o : $(BLIBDIR)/be_aas_route.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_routealt.o : $(BLIBDIR)/be_aas_routealt.cpp; $(DO_BOT_CC)
$(B)/ded/be_aas_sample.o : $(BLIBDIR)/be_aas_sample.cpp; $(DO_BOT_CC)
$(B)/ded/be_ai_char.o : $(BLIBDIR)/be_ai_char.cpp; $(DO_BOT_CC)
$(B)/ded/be_ai_chat.o : $(BLIBDIR)/be_ai_chat.cpp; $(DO_BOT_CC)
$(B)/ded/be_ai_gen.o : $(BLIBDIR)/be_ai_gen.cpp; $(DO_BOT_CC)
$(B)/ded/be_ai_goal.o : $(BLIBDIR)/be_ai_goal.cpp; $(DO_BOT_CC)
$(B)/ded/be_ai_move.o : $(BLIBDIR)/be_ai_move.cpp; $(DO_BOT_CC)
$(B)/ded/be_ai_weap.o : $(BLIBDIR)/be_ai_weap.cpp; $(DO_BOT_CC)
$(B)/ded/be_ai_weight.o : $(BLIBDIR)/be_ai_weight.cpp; $(DO_BOT_CC)
$(B)/ded/be_ea.o : $(BLIBDIR)/be_ea.cpp; $(DO_BOT_CC)
$(B)/ded/be_interface.o : $(BLIBDIR)/be_interface.cpp; $(DO_BOT_CC)
$(B)/ded/l_crc.o : $(BLIBDIR)/l_crc.cpp; $(DO_BOT_CC)
$(B)/ded/l_libvar.o : $(BLIBDIR)/l_libvar.cpp; $(DO_BOT_CC)
$(B)/ded/l_log.o : $(BLIBDIR)/l_log.cpp; $(DO_BOT_CC)
$(B)/ded/l_memory.o : $(BLIBDIR)/l_memory.cpp; $(DO_BOT_CC)
$(B)/ded/l_precomp.o : $(BLIBDIR)/l_precomp.cpp; $(DO_BOT_CC)
$(B)/ded/l_script.o : $(BLIBDIR)/l_script.cpp; $(DO_BOT_CC)
$(B)/ded/l_struct.o : $(BLIBDIR)/l_struct.cpp; $(DO_BOT_CC)

$(B)/ded/linux_signals.o : $(UDIR)/linux_signals.cpp; $(DO_DED_CC)
$(B)/ded/unix_main.o : $(UDIR)/unix_main.cpp; $(DO_DED_CC)
$(B)/ded/unix_shared.o : $(UDIR)/unix_shared.cpp; $(DO_DED_CC)

$(B)/ded/null_client.o : $(NDIR)/null_client.cpp; $(DO_DED_CC)
$(B)/ded/null_input.o : $(NDIR)/null_input.cpp; $(DO_DED_CC)
$(B)/ded/null_snddma.o : $(NDIR)/null_snddma.cpp; $(DO_DED_CC)
$(B)/ded/unzip.o : $(CMDIR)/unzip.cpp; $(DO_DED_CC)
$(B)/ded/vm.o : $(CMDIR)/vm.cpp; $(DO_DED_CC)
$(B)/ded/vm_interpreted.o : $(CMDIR)/vm_interpreted.cpp; $(DO_DED_CC)

$(B)/ded/ftola.o : $(UDIR)/ftola.s; $(DO_AS)
$(B)/ded/matha.o : $(UDIR)/matha.s; $(DO_AS)

$(B)/ded/vm_x86.o : $(CMDIR)/vm_x86.cpp; $(DO_DED_CC)
ifneq ($(VM_PPC),)
$(B)/ded/$(VM_PPC).o : $(CMDIR)/$(VM_PPC).cpp; $(DO_DED_CC)
endif


#############################################################################
# MISC
#############################################################################

copyfiles: build_release
	@if [ ! -d $(COPYDIR)/baseq3 ]; then echo "You need to set COPYDIR to where your Quake3 data is!"; fi

ifneq ($(BUILD_CLIENT),0)
	$(INSTALL) -s -m 0755 $(BR)/cnq3.$(ARCH)$(BINEXT) $(COPYDIR)/cnq3.$(ARCH)$(BINEXT)
endif

ifneq ($(BUILD_SERVER),0)
	@if [ -f $(BR)/cnq3ded.$(ARCH)$(BINEXT) ]; then \
		$(INSTALL) -s -m 0755 $(BR)/cnq3ded.$(ARCH)$(BINEXT) $(COPYDIR)/cnq3ded.$(ARCH)$(BINEXT); \
	fi
endif


clean: clean-debug clean-release
	$(MAKE) -C $(LOKISETUPDIR) clean

clean2:
	if [ -d $(B) ];then (find $(B) -name '*.d' -exec rm {} \;)fi
	rm -f $(Q3OBJ) $(Q3POBJ) $(Q3DOBJ) \
		$(MPGOBJ) $(Q3GOBJ) $(Q3CGOBJ) $(MPCGOBJ) $(Q3UIOBJ) $(MPUIOBJ) \
		$(MPGVMOBJ) $(Q3GVMOBJ) $(Q3CGVMOBJ) $(MPCGVMOBJ) $(Q3UIVMOBJ) $(MPUIVMOBJ)
	rm -f $(TARGETS)

clean-debug:
	$(MAKE) clean2 B=$(BD) CFLAGS="$(DEBUG_CFLAGS)"

clean-release:
	$(MAKE) clean2 B=$(BR) CFLAGS="$(RELEASE_CFLAGS)"

toolsclean:
	$(MAKE) -C $(TOOLSDIR)/asm clean uninstall
	$(MAKE) -C $(TOOLSDIR)/lcc clean uninstall

distclean: clean toolsclean
	rm -rf $(BUILD_DIR)

installer: build_release
	$(MAKE) VERSION=$(VERSION) -C $(LOKISETUPDIR)

dist:
	rm -rf quake3-$(SVN_VERSION)
	svn export . quake3-$(SVN_VERSION)
	tar --owner=root --group=root --force-local -cjf quake3-$(SVN_VERSION).tar.bz2 quake3-$(SVN_VERSION)
	rm -rf quake3-$(SVN_VERSION)


#############################################################################
# DEPENDENCIES
#############################################################################

.PHONY: release debug clean distclean copyfiles installer dist
