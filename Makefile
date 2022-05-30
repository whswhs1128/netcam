PORTING_ROOT_DIR ?= $(shell cd $(CURDIR)/../.. && /bin/pwd)

include $(PORTING_ROOT_DIR)/porting_base.mk

#编译所有子目录
#SUBDIRS=`ls -d */ | grep -v 'bin' | grep -v 'lib' | grep -v 'include'`

#编译指定子目录
SUBDIRS=live video_capture audio_capture main

define make_subdir
 @for subdir in $(SUBDIRS) ; do \
 ( cd $$subdir && make $1) \
 done;
endef

all:
	$(call make_subdir , all)

install:
	$(call make_subdir , install)

debug:
	$(call make_subdir , debug)

clean:
	$(call make_subdir , clean)
	rm -rf bin lib include