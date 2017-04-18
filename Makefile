#VERBOSE=1
#DEBUG=1

##########################################################################
# User configuration and firmware specific object files	
##########################################################################

# Overall system defines for compilation
ifdef DEBUG
GCC_DEFINE= -DDEBUG
OPT_LEVEL = 
else
GCC_DEFINE=
OPT_LEVEL = -O2
endif

GCC_DEFINE+= -std=gnu99

CFILES =
SFILES =
OLOC = ofiles
INCLUDE_PATHS = -I/usr/local/include/libusb-1.0 
LDLIBS = -L/usr/local/lib -lusb-1.0
DEBUG_OPTS = -g3 -gdwarf-2 -ggdb

##########################################################################
# Generic multi-project files 
##########################################################################

##########################################################################
# Project-specific files 
##########################################################################

# Main Files
# ==========
App_DIR=Src
INCLUDE_PATHS += -IInc -I$(OLOC)
CFILES += $(App_DIR)/itmDecoder.c $(App_DIR)/orbuculum.c $(App_DIR)/tpiuDecoder.c
CFILES += $(App_DIR)/generics.c

##########################################################################
# GNU GCC compiler prefix and location
##########################################################################

CROSS_COMPILE =
ASTYLE = astyle
AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
GDB = $(CROSS_COMPILE)gdb
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
GET_GIT_HASH = Tools/git_hash_to_c/git_hash_to_c.sh
MAKE = make
OUTFILE = orbuculum

##########################################################################
# Quietening
##########################################################################

ifdef VERBOSE
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

CFLAGS =  $(ARCH_FLAGS) $(STARTUP_DEFS) $(OPT_LEVEL) $(DEBUG_OPTS) \
		-ffunction-sections -fdata-sections -Wall $(INCLUDE_PATHS)  $(GCC_DEFINE)
ASFLAGS = -c $(DEBUG_OPTS) $(INCLUDE_PATHS) $(ARCH_FLAGS) $(GCC_DEFINE) \
          -x assembler-with-cpp
LDFLAGS = $(CFLAGS)

OCFLAGS = --strip-unneeded

OBJS =  $(patsubst %.c,%.o,$(CFILES)) $(patsubst %.s,%.o,$(SFILES))
POBJS = $(patsubst %,$(OLOC)/%,$(OBJS))
PDEPS =$(POBJS:.o=.d)

all : build 

get_version:
	$(Q)$(GET_GIT_HASH) > $(OLOC)/$(GIT_HASH_FILENAME)

$(OLOC)/%.o : %.c
	$(Q)mkdir -p $(basename $@)
	$(call cmd, \$(CC) -c $(CFLAGS) -MMD -o $@ $< ,\
	Compiling $<)

build: get_version $(POBJS) $(SYS_OBJS)
	$(Q)$(LD) $(LDFLAGS) -o $(OLOC)/$(OUTFILE) $(MAP) $(POBJS) $(LDLIBS)

tags:
	-@etags $(CFILES) 2> /dev/null

clean:
	-$(call cmd, \rm -f $(POBJS) $(LD_TEMP) $(OUTFILE) $(OUTFILE).map $(EXPORT) ,\
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
#The exclude is needed to prevent prettifying the cyclic link..no detrimental impact
	$(Q)-$(ASTYLE) --options=config/astyle.conf "*.h" "*.c"

-include $(PDEPS)
