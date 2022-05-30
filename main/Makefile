PORTING_ROOT_DIR ?= $(shell cd $(CURDIR)/../../.. && /bin/pwd)

include $(PORTING_ROOT_DIR)/porting_base.mk

##### Change the following for your environment:
CROSS_COMPILE?=		arm-gcc6.3-linux-uclibceabi-
COMPILE_OPTS =		$(INCLUDES) -I../lib -I../include -I../include/BasicUsageEnvironment -I../include/UsageEnvironment -I../include/groupsock -I../include/liveMedia -I. -O2 -DSOCKLEN_T=socklen_t -DNO_SSTREAM=1 -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 -DLOCALE_NOT_USED -lpthread
C =			c
C_COMPILER =		$(CROSS_COMPILE)gcc
C_FLAGS =		$(COMPILE_OPTS)
CPP =			cpp
CPLUSPLUS_COMPILER =	$(CROSS_COMPILE)g++
CPLUSPLUS_FLAGS =	$(COMPILE_OPTS) -Wall -DBSD=1
OBJ =			o
LINK =			$(CROSS_COMPILE)g++ -o
LINK_OPTS =		
CONSOLE_LINK_OPTS =	$(LINK_OPTS)
LIBRARY_LINK =		$(CROSS_COMPILE)ar cr 
LIBRARY_LINK_OPTS =	$(LINK_OPTS)
LIB_SUFFIX =			a
LIBS_FOR_CONSOLE_APPLICATION = -lssl -lcrypto -lpthread -ldl
LIBS_FOR_GUI_APPLICATION =
EXE =
##### End of variables to change

TARGET := netcam

C_FLAGS := $(PORTING_CFLAGS) $(PORTING_LIBS) -L../lib
CPLUSPLUS_FLAGS += $(PORTING_CFLAGS) $(PORTING_LIBS) -L../lib

SOURCES_ALL := $(shell find . -name "*.cpp" -o -name "*.c")
SOURCES_C   := $(filter %.c, $(SOURCES_ALL))
SOURCES_CPP := $(filter %.cpp, $(SOURCES_ALL))
OBJECTS_C   := $(addsuffix .o, $(basename $(SOURCES_C)))
OBJECTS_CPP := $(addsuffix .o, $(basename $(SOURCES_CPP)))
PREFIX = ..

.$(C).$(OBJ):
	$(C_COMPILER) -c $(C_FLAGS) $<
	@echo $<
.$(CPP).$(OBJ):
	$(CPLUSPLUS_COMPILER) -c $(CPLUSPLUS_FLAGS) $<
	@echo $<

##### OBJS = main.$(OBJ) video_capture.$(OBJ)
OBJS := $(OBJECTS_C) $(OBJECTS_CPP)
LIBS = -laudio_capture -lvideo_capture -lliveMedia -lgroupsock -lBasicUsageEnvironment -lUsageEnvironment -lssl -lcrypto -lpthread -ldl

.PHONY: all clean

all: $(OBJS)
	$(AT)$(LINK) $(TARGET) $^ $(C_FLAGS) $(LIBS) $(PORTING_LIBS)

clean:
	$(AT)rm -rf $(OBJS) $(TARGET)

install:all
	  install -d $(DESTDIR)$(PREFIX)/bin
	  install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin


