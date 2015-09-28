include prorab.mk


this_name := tests


#compiler flags
this_cflags += -std=c++11
this_cflags += -Wall
this_cflags += -DDEBUG
this_cflags += -fstrict-aliasing #strict aliasing!!!

this_srcs += main.cpp socket.cpp dns.cpp

this_ldlibs += -lsetka

ifeq ($(prorab_os),macosx)
    this_cflags += -stdlib=libc++ #this is needed to be able to use c++11 std lib
    this_ldlibs += -lc++
else ifeq ($(prorab_os),windows)
    this_ldlibs += -lws2_32
else
    this_ldlibs += -lpthread
endif

this_ldflags += -L$(prorab_this_dir)../../src/





ifeq ($(prorab_os),windows)
    this_test_cmd := (cd $(prorab_this_dir); cp ../../src/libsetka.dll .; ./$$(notdir $$^))
else ifeq ($(prorab_os),macosx)
    this_test_cmd := (cd $(prorab_this_dir); DYLD_LIBRARY_PATH=../../src ./$$(notdir $$^))
else
    this_test_cmd := (cd $(prorab_this_dir); LD_LIBRARY_PATH=../../src ./$$(notdir $$^))
endif


$(eval $(prorab-build-app))

define this_rule
testnet:: $(prorab_this_name)
	@echo running $$^...
	@$(this_test_cmd)
endef
$(eval $(this_rule))



#add dependency on libsetka.so
$(prorab_this_name): $(abspath $(prorab_this_dir)../../src/libsetka$(prorab_lib_extension))


#include makefile for building setka
$(eval $(call prorab-include,$(prorab_this_dir)../../src/makefile))
