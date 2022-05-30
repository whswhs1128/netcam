PORTING_ROOT_DIR ?= $(shell cd $(CURDIR)/../.. && /bin/pwd)

include $(PORTING_ROOT_DIR)/porting_base.mk

#����������Ŀ¼
#SUBDIRS=`ls -d */ | grep -v 'bin' | grep -v 'lib' | grep -v 'include'`

#����ָ����Ŀ¼
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