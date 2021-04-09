include prorab.mk

$(eval $(call prorab-config, ../config))

this_name := setka

this_soname := $(shell cat $(prorab_this_dir)soname.txt)

this_srcs := $(call prorab-src-dir, .)

this_ldlibs += -lopros -lnitki -lpapki -lutki

ifeq ($(os),windows)
    this_ldlibs += -lws2_32
endif

$(eval $(prorab-build-lib))
