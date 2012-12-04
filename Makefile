DEPS=$(shell pkg-config --cflags --libs opencv libavdevice libswscale)

all:
	LD_RUN_PATH=/home/josh/compiled/lib gcc -Wall -Wno-unused-function -g *.c $(DEPS) -I/home/josh/ccv/lib -L/home/josh/ccv/lib -lccv
