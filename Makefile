CONTIKI = /home/user/contiki/
all: $(CONTIKI_PROJECT)

CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
