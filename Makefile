# Build configuration
VERBOSE?=0
#DEBUG=1
SCREEN_HANDLING=1
MAKE_EXPERIMENTAL=1

# Set your preferred screen colours here, or create a new palette by copying the file to a new one
SCREEN_PALETTE="uicolours_default.h"
#SCREEN_PALETTE="uicolours_mono.h"

CFLAGS=-DVERSION="\"2.0.0Beta2InProgress\""

CROSS_COMPILE=
# Output Files
ORBLIB = orb
ORBUCULUM = orbuculum
ORBCAT    = orbcat
ORBFIFO   = orbfifo
ORBTOP    = orbtop
ORBDUMP   = orbdump
ORBSTAT   = orbstat
ORBMORTEM = orbmortem
ORBPROFILE= orbprofile

ifdef MAKE_EXPERIMENTAL
ORBTRACE  = orbtrace
else
ORBTRACE  =
endif
##########################################################################
# Check Host OS
##########################################################################

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CFLAGS += -DLINUX -D_GNU_SOURCE
  LINUX=1
endif
ifeq ($(UNAME_S),Darwin)
  CFLAGS += -DOSX
  OSX=1
endif

##########################################################################
# User configuration and firmware specific object files
##########################################################################

# Overall system defines for compilation
ifdef DEBUG
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
LDLIBS = -L. -L/usr/local/lib -lusb-1.0 -ldl -lncurses -lpthread -lintl -L$(OLOC) -l$(ORBLIB)
else
INCLUDE_PATHS += -I/usr/local/include/libusb-1.0
LDLIBS = -L. -L/usr/local/lib -lusb-1.0 -ldl -lncurses -L$(OLOC) -l$(ORBLIB)
endif

ifdef LINUX
LDLIBS += -lpthread
endif

##########################################################################
# Generic multi-project files
##########################################################################

##########################################################################
# Project-specific files
##########################################################################

# Main Files
# ==========

ORBLIB_CFILES = $(App_DIR)/itmDecoder.c $(App_DIR)/tpiuDecoder.c $(App_DIR)/msgDecoder.c $(App_DIR)/msgSeq.c $(App_DIR)/etmDecoder.c

ORBUCULUM_CFILES  = $(App_DIR)/$(ORBUCULUM).c $(App_DIR)/nwclient.c
ORBFIFO_CFILES    = $(App_DIR)/$(ORBFIFO).c $(App_DIR)/filewriter.c $(App_DIR)/itmfifos.c
ORBCAT_CFILES     = $(App_DIR)/$(ORBCAT).c
ORBTOP_CFILES     = $(App_DIR)/$(ORBTOP).c $(App_DIR)/symbols.c $(EXT)/cJSON.c
ORBDUMP_CFILES    = $(App_DIR)/$(ORBDUMP).c
ORBSTAT_CFILES    = $(App_DIR)/$(ORBSTAT).c $(App_DIR)/symbols.c $(App_DIR)/ext_fileformats.c
ORBMORTEM_CFILES  = $(App_DIR)/$(ORBMORTEM).c $(App_DIR)/symbols.c $(App_DIR)/sio.c
ORBPROFILE_CFILES = $(App_DIR)/$(ORBPROFILE).c $(App_DIR)/symbols.c $(App_DIR)/ext_fileformats.c
ORBTRACE_CFILES   = $(App_DIR)/$(ORBTRACE).c $(App_DIR)/orbtraceIf.c $(App_DIR)/symbols.c

##########################################################################
# GNU GCC compiler prefix and location
##########################################################################

ASTYLE = astyle
AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
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
           -ffunction-sections -fdata-sections -Wall -Wno-unused-result $(INCLUDE_PATHS) \
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

CFILES += $(App_DIR)/generics.c

##########################################################################
##########################################################################
##########################################################################

all : build

get_version:
	$(Q)mkdir -p $(OLOC)
	$(Q)$(GET_GIT_HASH) > $(OLOC)/$(GIT_HASH_FILENAME)

$(OLOC)/%.o : %.c
	$(Q)mkdir -p $(basename $@)
	$(call cmd, \$(CC) -c $(CFLAGS) -MMD -MP -o $@ $< ,\
	Compiling $<)

build: $(ORBUCULUM) $(ORBFIFO) $(ORBCAT) $(ORBTOP) $(ORBDUMP) $(ORBMORTEM) $(ORBPROFILE) $(ORBTRACE) $(ORBSTAT)

$(ORBLIB) : get_version $(ORBLIB_POBJS)
	$(Q)$(AR) rcs $(OLOC)/lib$(ORBLIB).a  $(ORBLIB_POBJS)
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

tags:
	-@etags $(CFILES) 2> /dev/null

clean:
	-$(call cmd, \rm -f $(POBJS) $(LD_TEMP) $(ORBUCULUM) $(ORBFIFO) $(ORBCAT) $(ORBDUMP) $(ORBSTAT) $(ORBMORTEM) $(ORBPROFILE) $(ORBTRACE) $(OUTFILE).map $(EXPORT) ,\
	Cleaning )
	$(Q)-rm -rf SourceDoc/*
	$(Q)-rm -rf *~ core
	$(Q)-rm -rf $(OLOC)/*
	$(Q)-rm -rf config/*~
	$(Q)-rm -rf TAGS

$(generated_dir)/git_head_revision.c:
	mkdir -p $(dir $@)
	../Tools/git_hash_to_c.sh > $@

doc:
	doxygen $(DOXCONFIG)

print-%:
	@echo $* is $($*)

pretty:
	$(Q)-$(ASTYLE) --options=config/astyle.conf "Inc/*.h" "Src/*.c"

-include $(PDEPS)
