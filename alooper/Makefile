	
	STRIP ?= strip

	# check on which OS we build
	UNAME_S := $(shell uname -s)

# get flags supported by CPU
ifeq ($(UNAME_S), FreeBSD)
  CPU_INFO = dmesg | grep Features | tr A-Z a-z
  ifeq ($(UNAME_M), amd64)
    UNAME_M = x86_64
  endif
else
  CPU_INFO = cat /proc/cpuinfo | grep flags
endif

	# check CPU and supported optimization flags
	ifneq ($$(filter $(CPU_INFO) | grep sse3 ) , )
		SSE_CFLAGS = -msse3 -mfpmath=sse
	else ifneq ($$(filter $(CPU_INFO) | grep sse2 ) , )
		SSE_CFLAGS = -msse2 -mfpmath=sse
	else ifneq ($$(filter $(CPU_INFO) | grep sse ) , )
		SSE_CFLAGS = -msse -mfpmath=sse
	else ifneq ($$(filter $(CPU_INFO) | grep ARM ) , )
		ifneq ($$$(filter $(CPU_INFO)  | grep ARMv7 ) , )
			ifneq ($$(filter $(CPU_INFO) | grep vfpd32 ) , )
				SSE_CFLAGS = -march=armv7-a -mfpu=vfpv3 
			else ifneq ($$(filter $(CPU_INFO) | grep vfpv3 ) , )
				SSE_CFLAGS = -march=armv7-a -mfpu=vfpv3
			endif
		else
			ARMCPU = "YES"
		endif
	else
		SSE_CFLAGS =
	endif

	# set bundle name
	NAME = alooper
	VER = 0.2

	PREFIX ?= /usr
	BIN_DIR ?= $(PREFIX)/bin
	SHARE_DIR ?= $(PREFIX)/share
	DESKAPPS_DIR ?= $(SHARE_DIR)/applications
	PIXMAPS_DIR ?= $(SHARE_DIR)/pixmaps

	LIB_DIR := ../libxputty/libxputty/
	HEADER_DIR := $(LIB_DIR)include/
	RESOURCES := $(wildcard $(RESOURCES_DIR)*.png)

	# set compile flags
	CFLAGS += -I. -O2 -Wall -funroll-loops `pkg-config --cflags sndfile jack`\
	-ffast-math -fomit-frame-pointer -fstrength-reduce -fdata-sections -Wl,--gc-sections \
	-pthread $(SSE_CFLAGS)
	CXXFLAGS += -MMD -std=c++11 $(CFLAGS)
	LDFLAGS += -I.  -lm -pthread -lpthread `pkg-config --libs sndfile jack`
	ifneq ($(MACOS)$(WINDOWS),true)
		LDFLAGS += -lrt -lc
	endif
	GUI_LDFLAGS += -I. -I$(HEADER_DIR) \
	-L. $(LIB_DIR)libxputty.a  `pkg-config --static --cflags --libs cairo x11` -lm 
	# invoke build files
	OBJECTS = xjack.c
	DEPS = alooper.d

.PHONY : mod all clean install uninstall 

all : check $(NAME)
	$(QUIET)mkdir -p ../bin
	$(QUIET)cp ./$(NAME) ../bin
	@if ! [ -f ../bin/$(NAME) ]; then \
		$(R_ECHO) "Sorry, build fail$(reset)"; \
	fi

-include $(DEPS)

check :
ifdef ARMCPU
	@$(R_ECHO)"ARM CPU DEDECTED, please check the optimization flags$(reset)"
endif


clean :
	$(QUIET)rm -f *.o *.d
	$(QUIET)rm -f $(NAME)
	$(QUIET)rm -rf ../bin

dist-clean :

install :
	$(QUIET)mkdir -p $(DESTDIR)$(BIN_DIR)
	$(QUIET)cp ./$(BUILD_DIR)/$(NAME) $(DESTDIR)$(BIN_DIR)/$(NAME)
	$(QUIET)mkdir -p $(DESTDIR)$(DESKAPPS_DIR)
	$(QUIET)cp $(NAME).desktop $(DESTDIR)$(DESKAPPS_DIR)
	$(QUIET)mkdir -p $(DESTDIR)$(PIXMAPS_DIR)
	$(QUIET)cp $(NAME).svg $(DESTDIR)$(PIXMAPS_DIR)

uninstall :
	$(QUIET)rm -rf $(DESTDIR)$(BIN_DIR)/$(NAME)
	$(QUIET)rm -rf $(DESTDIR)$(DESKAPPS_DIR)/$(NAME).desktop
	$(QUIET)rm -rf $(DESTDIR)$(PIXMAPS_DIR)/$(NAME).svg

$(NAME) : $(OBJECTS)
	@$(B_ECHO) "Build $@ $(reset)"
	$(QUIET)$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(NAME) $(LDFLAGS) $(GUI_LDFLAGS)
	$(QUIET)$(STRIP) -s -x -X -R .note.ABI-tag $(NAME)
	@$(B_ECHO) "=================== DONE =======================$(reset)"

doc:
	#pass
