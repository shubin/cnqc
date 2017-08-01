.NOTPARALLEL:

ifndef config
  config=release_x64
endif

ifndef verbose
  SILENT = @
endif

PROJECTS := server client

help:
	@echo "Usage: make [config=name] target"
	@echo ""
	@echo "CONFIGURATIONS:"
	@echo "  debug_x32"
	@echo "  debug_x64"
	@echo "  release_x32"
	@echo "  release_x64 (default)"
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
	@${MAKE} --no-print-directory -C ./makefiles/gmake cnq3-server config=$(config)
	
client:
	@${MAKE} --no-print-directory -C ./makefiles/gmake cnq3 config=$(config)
	
clean:
	@${MAKE} --no-print-directory -C ./makefiles/gmake clean config=$(config)

all: server client
