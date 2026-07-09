# ============================================================
#  PSPify - PSP Spotify Music Player Makefile
# ============================================================

TARGET = pspify
OBJS   = main.o

# CFLAGS includes O2 for optimization, -G0 for 8-byte global variable alignment
# We disable unused parameter warnings to compile cleanly.
CFLAGS = -O2 -G0 -Wall -Wno-unused-parameter

# Libraries to link:
#   -lpspgu: Graphics Utility (Hardware graphics)
#   -lpspmp3: Native hardware MP3 decoder
#   -lpspaudio: Hardware audio channels
#   -lpsprtc: Real-time clock for timing
#   -lm: Math library (used by stb_image)
LIBS = -lpspgu -lpspmp3 -lpspaudio -lpsprtc -lm

# PSP EBOOT metadata
EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = PSPify Player
PSP_EBOOT_ICON  = ICON0.PNG

# Include standard PSPSDK build mak rules
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
