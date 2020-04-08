include prorab.mk

this_name := tests

this_cxxflags += -std=c++11
this_cxxflags += -Wall
this_cxxflags += -DDEBUG
this_cxxflags += -fstrict-aliasing

this_srcs += main.cpp socket.cpp dns.cpp

this_ldlibs += -lsetka -lnitki -lpapki -lopros -lutki

ifeq ($(os),macosx)
    this_cxxflags += -stdlib=libc++ # this is needed to be able to use c++11 std lib
    this_ldlibs += -lc++
else ifeq ($(os),windows)
    this_ldlibs += -lws2_32
else
    this_ldlibs += -lpthread
endif

this_ldflags += -L$(d)../../src/

this_ldlibs += -lstdc++

ifeq ($(os),windows)
    this_test_cmd := (cd $(d); cp ../../src/libsetka.dll .; ./$$(notdir $$^))
else ifeq ($(os),macosx)
    this_test_cmd := (cd $(d); DYLD_LIBRARY_PATH=../../src ./$$(notdir $$^))
else
    this_test_cmd := (cd $(d); LD_LIBRARY_PATH=../../src ./$$(notdir $$^))
endif

$(eval $(prorab-build-app))

include $(d)../test_target.mk

# add dependency on libsetka.so
$(prorab_this_name): $(abspath $(d)../../src/libsetka$(dot_so))

# include makefile for building setka
$(eval $(call prorab-include,../../src/makefile))
