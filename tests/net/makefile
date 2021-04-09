include prorab.mk

$(eval $(call prorab-config, ../../config))

this_name := tests

this_no_install := true

this_srcs += $(call prorab-src-dir, .)

this_ldflags += -L$(d)../../src/out/$(c)
this_ldlibs += -lsetka -lnitki -lpapki -lopros -lutki

ifeq ($(os),windows)
    this_ldlibs += -lws2_32
endif

$(eval $(prorab-build-app))

include $(d)../test_target.mk

# add dependency on libsetka.so
$(prorab_this_name): $(abspath $(d)../../src/out/$(c)/libsetka$(dot_so))

# include makefile for building setka
$(eval $(call prorab-include, ../../src/makefile))
