# Native Palm-only UI; other platform source lists/flags remain unchanged.
FUJINET_NIO_LIB ?= ../fujinet-nio-lib
PALMOS_SDK_VERSION ?= 3.5
NIOLIB_BUILD ?= $(FUJINET_NIO_LIB)/build/palmos-shared-$(PALMOS_SDK_VERSION)
PALMOS_BUILD ?= build/palmos-usb
CC = m68k-palmos-gcc
FLAGS = -O2 -Wall -Werror -palmos$(PALMOS_SDK_VERSION) -I$(FUJINET_NIO_LIB)/include/palmos
ifeq ($(PALMOS_SDK_VERSION),3.1)
FLAGS += -DNIO_PALMOS_SDK31
endif
.PHONY: all
all: $(PALMOS_BUILD)/NioConfig.prc
$(PALMOS_BUILD):
	mkdir -p $@
$(PALMOS_BUILD)/NioConfig: src/platform/palmos/config.c $(FUJINET_NIO_LIB)/include/palmos/NioLib.h $(NIOLIB_BUILD)/libniolib-client.a | $(PALMOS_BUILD)
	$(CC) $(FLAGS) -Wl,-Map,$@.map -o $@ $< $(NIOLIB_BUILD)/libniolib-client.a
	m68k-palmos-nm -n $@ > $@.symbols
	m68k-palmos-objdump -d $@ > $@.dis
$(PALMOS_BUILD)/resources.stamp: src/platform/palmos/config.rcp | $(PALMOS_BUILD)
	pilrc -q $< $(PALMOS_BUILD)
	touch $@
ifneq ($(PALMOS_HIRES),1)
$(PALMOS_BUILD)/NioConfig.prc: $(PALMOS_BUILD)/NioConfig $(PALMOS_BUILD)/resources.stamp
	build-prc -c NiC1 -n "NIO Config" -o $@ $< $(PALMOS_BUILD)/tFRM03e8.bin $(PALMOS_BUILD)/tAIN03e8.bin
endif

# PALMOS_HIRES=1: the same app with hires/ (a colour 320x320 UI on Palm OS 5 high-density screens, the standard form
# elsewhere), built against the Palm OS 5 SDK headers for the high-density calls:
#   make -f makefiles/palmos.mk PALMOS_HIRES=1 FUJINET_NIO_LIB=... NIOLIB_BUILD=...
ifeq ($(PALMOS_HIRES),1)
H = src/platform/palmos/hires
HIRES_FLAGS = -O2 -Wall -palmos5r3 -DNIO_HIRES -I$(FUJINET_NIO_LIB)/include/palmos -I$(FUJINET_NIO_LIB)/include
# H2H_DIR=<head-to-head-test>/palm adds an H2H page (hires/h2h_page.c): the head-to-head speed test's suite, linked
# from there and built as its H2H NL app is, run through NioLib with its results sent to the shared h2h server.
# The page takes the code past 32 KB, further than one call reaches: one 64 KB code resource (text_64k), with the
# start-up code (libcrt) by crt0 and the libraries between the app and the suite, both within reach of them.
H2H_OBJS =
HIRES_LINK = src/platform/palmos/config.c $(H)/hires.c $(H)/text.c $(H)/gfx_palm.c $(NIOLIB_BUILD)/libniolib-client.a
ifneq ($(H2H_DIR),)
H2H_OBJS = $(addprefix $(PALMOS_BUILD)/h2h/,suite.o clock.o link_nio.o probe_niolib.o)
H2H_CFLAGS = -O2 -Wall -palmos5r3 -I$(FUJINET_NIO_LIB)/include -I$(FUJINET_NIO_LIB)/include/palmos \
             -DFN_MAX_PACKET_SIZE=1024 -DH2H_DEFAULT_CHUNK=1002 -DH2H_NIOLIB -DH2H_HOST_STACK
HIRES_FLAGS += -DNIO_H2H -I$(H2H_DIR)
HIRES_LINK = -T text_64k -lcrt src/platform/palmos/config.c $(H)/hires.c $(H)/text.c $(H)/gfx_palm.c \
             $(NIOLIB_BUILD)/libniolib-client.a -lc -lgcc $(H2H_OBJS) $(NIOLIB_BUILD)/libniolib-client.a
$(PALMOS_BUILD)/h2h/%.o: $(H2H_DIR)/%.c $(H2H_DIR)/h2h.h | $(PALMOS_BUILD)
	mkdir -p $(@D)
	$(CC) $(H2H_CFLAGS) -c -o $@ $<
endif
$(PALMOS_BUILD)/NioConfigHD: src/platform/palmos/config.c $(H)/hires.c $(H)/h2h_page.c $(H)/text.c $(H)/gfx_palm.c $(wildcard $(H)/*.h) $(H2H_OBJS) $(NIOLIB_BUILD)/libniolib-client.a | $(PALMOS_BUILD)
	$(CC) $(HIRES_FLAGS) -Wl,-Map,$@.map -o $@ $(HIRES_LINK)
	m68k-palmos-size $@
$(PALMOS_BUILD)/hires.stamp: $(H)/hires.rcp src/platform/palmos/config.rcp $(wildcard $(H)/fonts/*) | $(PALMOS_BUILD)
	mkdir -p $(PALMOS_BUILD)/hires
	pilrc -q src/platform/palmos/config.rcp $(PALMOS_BUILD)/hires
	cd $(H) && pilrc -q hires.rcp $(abspath $(PALMOS_BUILD))/hires
	touch $@
$(PALMOS_BUILD)/NioConfig.prc: $(PALMOS_BUILD)/NioConfigHD $(PALMOS_BUILD)/hires.stamp
	build-prc -c NiC1 -n "NIO Config" -o $@ $< $(PALMOS_BUILD)/hires/*.bin
endif
