#---------------------------------------------------------------------------------
# Orbitto - Nintendo Switch NRO (SDL2). Based on the libnx template Makefile.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := Orbitto
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include
ROMFS       := romfs

APP_TITLE   := Orbitto
APP_AUTHOR  := Amir 14 Games
APP_VERSION := 1.0.0
ICON        := icon.jpg

PORTLIBS_PATH ?= $(DEVKITPRO)/portlibs
PORTLIBS_SW   := $(PORTLIBS_PATH)/switch
PKGCONF       := $(PORTLIBS_SW)/bin/aarch64-none-elf-pkg-config

PKGCFLAGS := $(shell $(PKGCONF) --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null)
PKGLIBS   := $(shell $(PKGCONF) --libs --static sdl2 SDL2_image SDL2_ttf 2>/dev/null)
ifeq ($(strip $(PKGLIBS)),)
PKGLIBS := -lSDL2_image -lSDL2_ttf -lSDL2 -lfreetype -lpng -ljpeg -lwebp -lbz2 -lz -lEGL -lGLESv2 -lglapi -ldrm_nouveau
endif

ARCH   := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS += $(INCLUDE) $(PKGCFLAGS)
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := $(PKGLIBS) -lnx
LIBDIRS := $(PORTLIBS_SW) $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/$(ICON)
export NROFLAGS += --icon=$(APP_ICON)
ifneq ($(ROMFS),)
export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all : $(OUTPUT).nro

$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)

$(OFILES_SRC) : $(HFILES_BIN)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
