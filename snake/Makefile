# ============================================================
#  PSP Makefile – Snake game  (pspgu rendering)
#
#  LIBS only lists libraries NOT already included by build.mak:
#    pspge, pspdisplay, pspctrl, pspdebug are added automatically.
#    pspgu must be added explicitly.
# ============================================================

TARGET  = snake
OBJS    = main.o

CFLAGS  = -O2 -G0 -Wall -Wno-unused-parameter
LIBS    = -lpspgu -lpsprtc -lm

EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = Snake v1.0
PSP_EBOOT_ICON  = ICON0.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
