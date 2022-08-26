# Build configuration
VERBOSE?=0

# Set DEBUG=0 for release version
DEBUG?=1
SCREEN_HANDLING=1
MAKE_EXPERIMENTAL=1

ORBLIB=orb
LIBSONAME=lib$(ORBLIB).so.1
LIBVER=0

SCREEN_PALETTE="uicolours_default.h"

CFLAGS=

# Add these flags to be evil, but in a nice way
#CFLAGS+=-fsanitize=address -static-libasan
#CFLAGS+=-fanalyzer

INSTALL_ROOT?=/usr/local/

CROSS_COMPILE=
# Output Files
ORBUCULUM = orbuculum
ORBCAT    = orbcat
ORBFIFO   = orbfifo
ORBTOP    = orbtop
ORBDUMP   = orbdump
ORBSTAT   = orbstat
ORBMORTEM = orbmortem
ORBTRACE  = orbtrace
ORBZMQ    = orbzmq

ifdef MAKE_EXPERIMENTAL
ORBPROFILE= orbprofile
else
ORBPROFILE  =
endif
##########################################################################
# Check Host OS
##########################################################################

ifeq ($(OS),Windows_NT)     # is Windows_NT on XP, 2000, 7, Vista, 10...
    WINDOWS=1
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CFLAGS += -DLINUX -D_GNU_SOURCE
        LINUX=1
    endif
    ifeq ($(UNAME_S),Darwin)
        CFLAGS += -DOSX
        OSX=1
    endif
endif

##########################################################################
# User configuration and firmware specific object files
##########################################################################

# Overall system defines for compilation
ifeq ($(DEBUG),1)
GCC_DEFINE= -DDEBUG
DEBUG_OPTS = -g3 -gdwarf-2 -ggdb3
OPT_LEVEL = -Og
else
GCC_DEFINE=
DEBUG_OPTS =
OPT_LEVEL = -O2
endif

ifeq ($(WITH_FIFOS),1)
CFLAGS += -DWITH_FIFOS
endif

ifeq ($(SCREEN_HANDLING),1)
CFLAGS += -DSCREEN_HANDLING
endif

ifeq ($(WITH_NWCLIENT),1)
CFLAGS += -DWITH_NWCLIENT
endif

# Directories for sources
App_DIR=Src
Inc_DIR=Inc
EXT=$(App_DIR)/external
EXTINC=$(Inc_DIR)/external
INCLUDE_PATHS = -I$(Inc_DIR) -I$(EXTINC) -I$(OLOC)

GCC_DEFINE+= -std=gnu99

CFILES =
SFILES =
OLOC = ofiles

ifdef OSX
INCLUDE_PATHS += -I/usr/local/include/libusb-1.0
LDLIBS = -L.  -L$(OLOC) -l$(ORBLIB) -L/usr/local/lib -lusb-1.0 -ldl -lncurses -lintl
else
	ifdef WINDOWS
		INCLUDE_PATHS += -I/usr/local/include/libusb-1.0
		LDLIBS = -L. -L$(OLOC) -l$(ORBLIB) -L/usr/local/lib -lusb-1.0 -lncursesw -lintl 
	else
		INCLUDE_PATHS += -I/usr/local/include/libusb-1.0
		LDLIBS = -L. -L$(OLOC) -l$(ORBLIB) -L/usr/local/lib -lusb-1.0 -ldl -lncurses 
	endif
endif

ifdef WINDOWS
LDLIBS += -lWs2_32
endif

ifdef LINUX
LDLIBS += -ldl
endif

##########################################################################
# Generic multi-project files
##########################################################################

##########################################################################
# Project-specific files
##########################################################################

# Main Files
# ==========

ORBLIB_CFILES = $(App_DIR)/itmDecoder.c $(App_DIR)/tpiuDecoder.c $(App_DIR)/msgDecoder.c $(App_DIR)/msgSeq.c $(App_DIR)/traceDecoder.c

ifdef WINDOWS
	ORBLIB_CFILES += $(App_DIR)/stream_win32.c $(App_DIR)/stream_file_win32.c $(App_DIR)/stream_socket_win32.c
else
	ORBLIB_CFILES += $(App_DIR)/stream_file_posix.c $(App_DIR)/stream_socket_posix.c
endif

ORBUCULUM_CFILES  = $(App_DIR)/$(ORBUCULUM).c $(App_DIR)/nwclient.c
ORBFIFO_CFILES    = $(App_DIR)/$(ORBFIFO).c $(App_DIR)/filewriter.c $(App_DIR)/itmfifos.c
ORBCAT_CFILES     = $(App_DIR)/$(ORBCAT).c
ORBTOP_CFILES     = $(App_DIR)/$(ORBTOP).c $(App_DIR)/symbols.c $(EXT)/cJSON.c
ORBDUMP_CFILES    = $(App_DIR)/$(ORBDUMP).c
ORBSTAT_CFILES    = $(App_DIR)/$(ORBSTAT).c $(App_DIR)/symbols.c $(App_DIR)/ext_fileformats.c
ORBMORTEM_CFILES  = $(App_DIR)/$(ORBMORTEM).c $(App_DIR)/symbols.c $(App_DIR)/sio.c
ORBPROFILE_CFILES = $(App_DIR)/$(ORBPROFILE).c $(App_DIR)/symbols.c $(App_DIR)/ext_fileformats.c
ORBTRACE_CFILES   = $(App_DIR)/$(ORBTRACE).c $(App_DIR)/orbtraceIf.c $(App_DIR)/symbols.c
ORBZMQ_CFILES     = $(App_DIR)/$(ORBZMQ).c

##########################################################################
# GNU GCC compiler prefix and location
##########################################################################

ASTYLE = astyle
AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
LN = ln
GDB = $(CROSS_COMPILE)gdb
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
GET_GIT_HASH = Tools/git_hash_to_c/git_hash_to_c.sh
MAKE = make

##########################################################################
# Quietening
##########################################################################

ifeq ($(VERBOSE),1)
cmd = $1
Q :=
else
cmd = @$(if $(value 2),echo "$2";)$1
Q := @
endif

HOST=-lc -lusb

##########################################################################
# Compiler settings, parameters and flags
##########################################################################
# filename for embedded git revision
GIT_HASH_FILENAME=git_version_info.h

CFLAGS +=  $(ARCH_FLAGS) $(STARTUP_DEFS) $(OPT_LEVEL) $(DEBUG_OPTS) \
           -Wall -Wno-unused-result $(INCLUDE_PATHS) \
           -include $(SCREEN_PALETTE) $(GCC_DEFINE)
ASFLAGS += -c $(DEBUG_OPTS) $(INCLUDE_PATHS) $(ARCH_FLAGS) $(GCC_DEFINE) \
           -x assembler-with-cpp
LDFLAGS += $(CFLAGS)

OCFLAGS += --strip-unneeded

# Generic Stuff
OBJS =  $(patsubst %.c,%.o,$(CFILES)) $(patsubst %.s,%.o,$(SFILES))
POBJS = $(patsubst %,$(OLOC)/%,$(OBJS))
PDEPS = $(POBJS:.o=.d)

# Per Target Stuff
ORBLIB_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBLIB_CFILES))
ORBLIB_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBLIB_OBJS))
PDEPS += $(ORBLIB_POBJS:.o=.d)

ORBUCULUM_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBUCULUM_CFILES))
ORBUCULUM_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBUCULUM_OBJS))
PDEPS += $(ORBUCULUM_POBJS:.o=.d)

ORBFIFO_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBFIFO_CFILES))
ORBFIFO_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBFIFO_OBJS))
PDEPS += $(ORBFIFO_POBJS:.o=.d)

ORBCAT_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBCAT_CFILES))
ORBCAT_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBCAT_OBJS))
PDEPS += $(ORBCAT_POBJS:.o=.d)

ORBTOP_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBTOP_CFILES))
ORBTOP_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBTOP_OBJS))
PDEPS += $(ORBTOP_POBJS:.o=.d)

ORBDUMP_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBDUMP_CFILES))
ORBDUMP_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBDUMP_OBJS))
PDEPS += $(ORBDUMP_POBJS:.o=.d)

ORBSTAT_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBSTAT_CFILES))
ORBSTAT_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBSTAT_OBJS))
PDEPS += $(ORBSTAT_POBJS:.o=.d)

ORBMORTEM_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBMORTEM_CFILES))
ORBMORTEM_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBMORTEM_OBJS))
PDEPS += $(ORBMORTEM_POBJS:.o=.d)

ORBPROFILE_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBPROFILE_CFILES))
ORBPROFILE_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBPROFILE_OBJS))
PDEPS += $(ORBPROFILE_POBJS:.o=.d)

ORBTRACE_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBTRACE_CFILES))
ORBTRACE_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBTRACE_OBJS))
PDEPS += $(ORBTRACE_POBJS:.o=.d)

ORBZMQ_OBJS =  $(OBJS) $(patsubst %.c,%.o,$(ORBZMQ_CFILES))
ORBZMQ_POBJS = $(POJBS) $(patsubst %,$(OLOC)/%,$(ORBZMQ_OBJS))
PDEPS += $(ORBZMQ_POBJS:.o=.d)

CFILES += $(App_DIR)/generics.c

BUILDABLES = $(ORBUCULUM) $(ORBCAT) $(ORBTOP) $(ORBDUMP) $(ORBMORTEM) $(ORBPROFILE) $(ORBTRACE) $(ORBSTAT) $(ORBZMQ)
ifndef WINDOWS
BUILDABLES += $(ORBFIFO)
endif

##########################################################################
##########################################################################
##########################################################################

all : build

.PHONY: get_version
get_version:
	$(Q)mkdir -p $(OLOC)
	$(Q)echo "#define GIT_DESCRIBE \"`git describe --tags --always --dirty`\"" > $(OLOC)/$(GIT_HASH_FILENAME).comp
	$(Q)diff -q $(OLOC)/$(GIT_HASH_FILENAME).comp $(OLOC)/$(GIT_HASH_FILENAME) > /dev/null 2>&1 || cp $(OLOC)/$(GIT_HASH_FILENAME).comp $(OLOC)/$(GIT_HASH_FILENAME)

.PHONY: check_debug
check_debug:
	$(Q)mkdir -p $(OLOC)
ifeq ($(DEBUG),1)
	$(Q)touch $(OLOC)/.isdebug
	@echo Perform \"LD_LIBRARY_PATH=$(PWD)/$(OLOC)\"
else
	@echo Perform "export LD_LIBRARY_PATH="
endif

$(OLOC)/%.o : %.c
	$(Q)mkdir -p $(basename $@)
	$(call cmd, \$(CC) -c $(CFLAGS) -MMD -MP -o $@ $< ,\
	Compiling $<)

build: check_debug $(BUILDABLES)

$(ORBLIB_POBJS): CFLAGS += -fPIC

$(ORBLIB) : get_version $(ORBLIB_POBJS)
	$(Q)$(CC) -shared -Wl,-soname,$(LIBSONAME) -o $(OLOC)/$(LIBSONAME).$(LIBVER) $(ORBLIB_POBJS) -lc
	$(Q)$(RM) -f $(OLOC)/lib$(ORBLIB).so
	$(Q)ldconfig -n $(OLOC)
	$(Q)$(LN) -s $(LIBSONAME) $(OLOC)/lib$(ORBLIB).so
	-@echo "Completed build of" $(ORBLIB)

$(ORBUCULUM) : $(ORBLIB) $(ORBUCULUM_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBUCULUM) $(MAP) $(ORBUCULUM_POBJS)  $(LDLIBS)
	-@echo "Completed build of" $(ORBUCULUM)

$(ORBFIFO) : $(ORBLIB) $(ORBFIFO_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBFIFO) $(MAP) $(ORBFIFO_POBJS)  $(LDLIBS)
	-@echo "Completed build of" $(ORBFIFO)

$(ORBCAT) : $(ORBLIB) $(ORBCAT_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBCAT) $(MAP) $(ORBCAT_POBJS) $(LDLIBS)
	-@echo "Completed build of" $(ORBCAT)

$(ORBTOP) : $(ORBLIB) $(ORBTOP_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBTOP) $(MAP) $(ORBTOP_POBJS) $(LDLIBS)
	-@echo "Completed build of" $(ORBTOP)

$(ORBDUMP) : $(ORBLIB) $(ORBDUMP_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBDUMP) $(MAP) $(ORBDUMP_POBJS) $(LDLIBS)
	-@echo "Completed build of" $(ORBDUMP)

$(ORBSTAT) : $(ORBLIB) $(ORBSTAT_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBSTAT) $(MAP) $(ORBSTAT_POBJS) $(LDLIBS)
	-@echo "Completed build of" $(ORBSTAT)

$(ORBMORTEM) : $(ORBLIB) $(ORBMORTEM_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBMORTEM) $(MAP) $(ORBMORTEM_POBJS)  $(LDLIBS)
	-@echo "Completed build of" $(ORBMORTEM)

$(ORBPROFILE) : $(ORBLIB) $(ORBPROFILE_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBPROFILE) $(MAP) $(ORBPROFILE_POBJS)  $(LDLIBS)
	-@echo "Completed build of" $(ORBPROFILE)

$(ORBTRACE) : $(ORBTRACE_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBTRACE) $(MAP) $(ORBTRACE_POBJS)  $(LDLIBS)
	-@echo "Completed build of" $(ORBTRACE)

$(ORBZMQ) : $(ORBLIB) $(ORBZMQ_POBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(ORBZMQ) $(MAP) $(ORBZMQ_POBJS) $(LDLIBS) -lzmq
	-@echo "Completed build of" $(ORBZMQ)

install:
ifneq ("$(wildcard $(OLOC)/.isdebug)","")
	$(error Please build non-debug version prior to install)
endif
	$(Q)install -D $(OLOC)/$(ORBUCULUM) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBCAT) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBTOP) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBMORTEM) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBPROFILE) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBTRACE) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBSTAT) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBDUMP) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBZMQ) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(LIBSONAME).$(LIBVER) --target-directory=$(DESTDIR)$(INSTALL_ROOT)lib
	$(Q)ldconfig
	$(Q)$(LN) -sf $(LIBSONAME) $(DESTDIR)$(INSTALL_ROOT)lib/lib$(ORBLIB).so

ifndef WINDOWS
	$(Q)install -D $(OLOC)/$(ORBFIFO) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)install -D $(OLOC)/$(ORBSTAT) --target-directory=$(DESTDIR)$(INSTALL_ROOT)bin
endif

	$(Q)install -D -m 644 Support/gdbtrace.init --target-directory=$(DESTDIR)$(INSTALL_ROOT)share/orbcode
	-@echo "Install complete"

ifdef WINDOWS
install-mingw-deps:
	$(Q)ldd $(OLOC)/$(ORBUCULUM) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBCAT) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBTOP) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBMORTEM) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBMORTEM) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBTRACE) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBSTAT) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBDUMP) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
	$(Q)ldd $(OLOC)/$(ORBZMQ) | grep -vi System32 | gawk '{ print $$3 }' | xargs -rt cp -t $(DESTDIR)$(INSTALL_ROOT)bin
endif

uninstall:
	$(Q)rm -f $(DESTDIR)$(INSTALL_ROOT)bin/$(ORBUCULUM) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBFIFO) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBCAT) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBDUMP) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBSTAT) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBTOP) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBMORTEM) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBPROFILE) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBTRACE) \
		$(DESTDIR)$(INSTALL_ROOT)bin/$(ORBZMQ) \
		$(DESTDIR)$(INSTALL_ROOT)share/orbcode/gdbtrace.init \
		$(DESTDIR)$(INSTALL_ROOT)lib/lib$(ORBLIB).so*
	-@echo "Uninstall complete"

clean:
	-$(call cmd, \rm -rf $(OLOC) $(EXPORT) ,\
	Cleaning )
	$(Q)-rm -rf SourceDoc/*
	$(Q)-rm -rf *~ core
	$(Q)-rm -rf config/*~
	$(Q)-rm -rf TAGS

doc:
	doxygen $(DOXCONFIG)

print-%:
	@echo $* is $($*)

pretty:
	$(Q)-$(ASTYLE) --options=config/astyle.conf "Inc/*.h" "Src/*.c"

-include $(PDEPS)

