############################################################
# Variables setable from the command line:
#
# CCOMPILER (default: clang)
# DEBUG_SYMBOLS (default: DWARF)
# EXTRA_CFLAGS (default: none)
############################################################

ifndef CCOMPILER
CC=clang
else
CC=$(CCOMPILER)
endif

ifeq ($(OS),Windows_NT)
OSNAME:=$(OS)
else
OSNAME:=$(shell uname -s)
endif

BASENAME=lined

LIBOSE_DIR=../libose
OSE_CFILES=\
	ose.c\
	ose_util.c\
	ose_stackops.c\
	ose_match.c\
	ose_context.c\
	ose_print.c

MOD_FILES=\
	ose_$(BASENAME).c

INCLUDES=-I. -I$(LIBOSE_DIR)

DEFINES=-DHAVE_OSE_ENDIAN_H

CFLAGS_DEBUG=-Wall -DOSE_CONF_DEBUG -O0 -g$(DEBUG_SYMBOLS) $(EXTRA_CFLAGS)
CFLAGS_RELEASE=-Wall -O3 $(EXTRA_CFLAGS)

release: CFLAGS+=$(CFLAGS_RELEASE)
release: $(LIBOSE_DIR)/sys/ose_endian.h ose_$(BASENAME).so

debug: CFLAGS+=$(CFLAGS_DEBUG)
debug: $(LIBOSE_DIR)/sys/ose_endian.h ose_$(BASENAME).so

ifeq ($(OS),Windows_NT)
else
CFLAGS:=-fPIC
endif

ose_$(BASENAME).so: $(foreach f,$(OSE_CFILES),$(LIBOSE_DIR)/$(f)) $(MOD_FILES)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -shared -o o.se.$(BASENAME).so $^

$(LIBOSE_DIR)/sys/ose_endian.h:
	cd $(LIBOSE_DIR) && $(MAKE) sys/ose_endian.h

.PHONY: clean
clean:
	rm -rf *.o *.so *.dSYM
