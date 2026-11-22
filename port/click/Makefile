OSNAME := $(shell uname -s)

LINUX_SOUND_OBJ = sound_alsa.o
LINUX_SOUND_LIB = -lasound

override CFLAGS += -Wall -std=gnu11

ifeq ($(OSNAME), Darwin)
OSXLIBS = -framework AudioUnit -framework AudioToolbox -framework CoreServices \
	-framework CoreAudio -framework CoreFoundation -framework Foundation
override CFLAGS += -ObjC
SOUND_OBJS += sound_macosx.o
LIBS = $(OSXLIBS)
else
SOUND_OBJS += $(LINUX_SOUND_OBJ)
LIBS = -lpthread -lm $(LINUX_SOUND_LIB)
endif

CLICK = click
CLICK_OBJS = click.o libclicker.a

PROGRAMS = $(CLICK)

all: CFLAGS += -O2

debug: CFLAGS += -g -Ddebug

all:	$(PROGRAMS)
debug:  $(PROGRAMS)

# link libclicker.a + -lpthread -lm -lasound into your own project
libclicker.a: clicker.o $(SOUND_OBJS)
	$(AR) rcs $@ $^

$(CLICK):	$(CLICK_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(CLICK_OBJS) $(LIBS)

sound_macosx.o:	sound_macosx.c sound.h
sound_alsa.o:	sound_alsa.c sound.h
clicker.o:	clicker.c clicker.h sound.h
click.o:        click.c clicker.h sound.h

clean:
	rm -f *.o libclicker.a $(PROGRAMS)
