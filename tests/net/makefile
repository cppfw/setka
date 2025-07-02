include prorab.mk
include prorab-test.mk

$(eval $(call prorab-config, ../../config))

this_name := tests

this_no_install := true

this_srcs += $(call prorab-src-dir, .)

this_ldlibs += -l nitki$(this_dbg)
this_ldlibs += -l papki$(this_dbg)
this_ldlibs += -l opros$(this_dbg)
this_ldlibs += -l utki$(this_dbg)

this_ldlibs += ../../src/out/$(c)/libsetka$(this_dbg)$(dot_so)

ifeq ($(os),windows)
    this_ldlibs += -l ws2_32
endif

$(eval $(prorab-build-app))

this_test_cmd := $(prorab_this_name)
this_test_deps := $(prorab_this_name)
this_test_ld_path := ../../src/out/$(c)
$(eval $(prorab-test))

# include makefile for building setka
$(eval $(call prorab-include, ../../src/makefile))
