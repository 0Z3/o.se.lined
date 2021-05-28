BASENAME=lined

CC=clang

OSE_DIR=../..
OSE_CFILES=\
	ose.c\
	ose_util.c\
	ose_stackops.c\
	ose_match.c\
	ose_context.c\
	ose_assert.c\
	ose_print.c\
	sys/ose_term.c\

MOD_FILES=\
	ose_$(BASENAME).c

INCLUDES=-I. -I$(OSE_DIR)

DEFINES=-DHAVE_OSE_ENDIAN_H

CFLAGS_DEBUG=-Wall -DOSE_CONF_DEBUG -O0 -glldb
CFLAGS_RELEASE=-Wall -O3

release: CFLAGS=$(CFLAGS_RELEASE)
release: ose_$(BASENAME).so

debug: CFLAGS=$(CFLAGS_DEBUG)
debug: ose_$(BASENAME).so

# %.o: $(OSE_DIR)/$(OSE_CFILES)
# 	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

ose_$(BASENAME).so: $(foreach f,$(OSE_CFILES),$(OSE_DIR)/$(f)) $(MOD_FILES)	
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -shared -o $@ $^

$(OSE_DIR)/sys/ose_endian.h:
	cd $(OSE_DIR) && $(MAKE) sys/ose_endian.h

.PHONY: clean
clean:
	rm -rf *.o *.so
