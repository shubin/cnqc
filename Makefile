.NOTPARALLEL:

host_os := $(shell uname -s)
ifeq ($(host_os), FreeBSD)
	make_dir := ./makefiles/bsd_gmake
	make_name := gmake
else
	make_dir := ./makefiles/linux_gmake
	make_name := make
endif

ifndef config
  config=release
endif

ifndef verbose
  SILENT = @
endif

PROJECTS := server client

help:
	@echo "Usage: $(make_name) [config=name] target"
	@echo ""
	@echo "CONFIGURATIONS:"
	@echo "  release (default)"
	@echo "  debug"
	@echo ""
	@echo "TARGETS:"
	@echo "  server"
	@echo "  client"
	@echo "  all (server + client)"
	@echo "  clean"
	@echo "  help (default)"
	@echo ""
	@echo "REQUIREMENTS:"
	@echo "  NASM is required for building the client."
	@echo ""
	@echo "OPTIONS:"
	@echo "  If the environment variable QUAKE3DIR is defined,"
	@echo "  the output executable(s) will be copied to"
	@echo "  that directory as a post-build command."

server:
	@${MAKE} --no-print-directory -C $(make_dir) cnq3-server config=$(config)_x64
	
client:
	@${MAKE} --no-print-directory -C $(make_dir) cnq3 config=$(config)_x64
	
clean:
	@${MAKE} --no-print-directory -C $(make_dir) clean config=$(config)_x64

all: server client
