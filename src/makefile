include prorab.mk
include prorab-license.mk
include prorab-clang-format.mk

$(eval $(call prorab-config, ../config))

this_name := setka

this_soname := $(shell cat $(d)soname.txt)

this_srcs := $(call prorab-src-dir, .)

this_ldlibs += -lopros -lnitki -lpapki -lutki

ifeq ($(os),windows)
    this_ldlibs += -lws2_32
endif

$(eval $(prorab-build-lib))

$(eval $(prorab-clang-format))

this_src_dir := $(this_name)
this_license_file := ../LICENSE
$(eval $(prorab-license))
