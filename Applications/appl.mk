ifeq ($(target),)
target=stm32h743
endif

CFLAGS=-Wall -Wextra -g -c -O0 -MD -std=gnu99
CFLAGS+=$(EXTRA_CFLAGS)

ifeq ($(target),stm32h743)
TOOLCHAIN_PATH=../../buildroot/output/host/bin/
TOOLCHAIN=$(TOOLCHAIN_PATH)arm-buildroot-uclinux-uclibcgnueabi-
CFLAGS+=-mcpu=cortex-m4 -funwind-tables
##CFLAGS+=-O2 -fno-omit-frame-pointer
OBJDIR=.obj/stm32h743
EXEC=$(EXE)
endif

ifeq ($(target),host)
EXEC=$(EXE)_h
endif

CC=$(TOOLCHAIN)gcc
LD=$(TOOLCHAIN)gcc
AR=$(TOOLCHAIN)ar
STRIP=$(TOOLCHAIN)strip

OBJDIR=.obj/$(target)
OBJS= $(addprefix $(OBJDIR)/, $(SRCS:.c=.o))

$(OBJDIR)/%o: %c
	$(CC) $(CFLAGS) $< -o $@

all: $(OBJDIR)/ $(EXEC)

$(EXEC): $(OBJS) $(LINKER_SCRIPT)
	$(LD) $(OBJS) $(LDFLAGS) -pthread -o $@ -lrt

$(OBJDIR)/:
	mkdir -p $(OBJDIR)

clean:
	rm -Rf $(OBJDIR) $(EXEC) $(EXEC)_s *~

clean_all: clean
	rm -Rf .obj $(EXE) $(EXE)_s $(EXE)_a $(EXE)_a_s $(EXE)_h $(EXE)_h_s

-include $(OBJS:.o=.d)

.PHONY: all clean clean_all



