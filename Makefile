BASENAME=lined

CC=clang

LIBOSE_DIR=../libose
OSE_CFILES=\
	ose.c\
	ose_util.c\
	ose_stackops.c\
	ose_match.c\
	ose_context.c\
	ose_assert.c\
	ose_print.c

MOD_FILES=\
	ose_$(BASENAME).c

INCLUDES=-I. -I$(LIBOSE_DIR)

DEFINES=-DHAVE_OSE_ENDIAN_H

CFLAGS_DEBUG=-Wall -DOSE_CONF_DEBUG -O0 -glldb -fPIC
CFLAGS_RELEASE=-Wall -O3 -fPIC

release: CFLAGS=$(CFLAGS_RELEASE)
release: $(LIBOSE_DIR)/sys/ose_endian.h ose_$(BASENAME).so

debug: CFLAGS=$(CFLAGS_DEBUG)
debug: $(LIBOSE_DIR)/sys/ose_endian.h ose_$(BASENAME).so

ose_$(BASENAME).so: $(foreach f,$(OSE_CFILES),$(LIBOSE_DIR)/$(f)) $(MOD_FILES)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -shared -o o.se.$(BASENAME).so $^

$(LIBOSE_DIR)/sys/ose_endian.h:
	cd $(LIBOSE_DIR) && $(MAKE) sys/ose_endian.h

.PHONY: clean
clean:
	rm -rf *.o *.so *.dSYM
