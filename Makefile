ENV=LD_RUN_PATH=/home/josh/compiled/lib
CFLAGS=-Wall -Wextra -Wno-unused-function -D_GNU_SOURCE -g
DEPS=$(shell pkg-config --cflags --libs opencv libavdevice libswscale)

OTHER=test stream face histogram hc bkg patch kdtree fill
OBJS=encode.o capture.o

all: kdtree

$(OBJS): %.o : %.h
	$(ENV) gcc $(CFLAGS) $(DEPS) -c $(OBJS:.o=.c)

motion: $(OBJS)
	$(ENV) gcc $(CFLAGS) $^ motion.c $(DEPS)

ccv: capture.o
	$(ENV) gcc $(CFLAGS) $^ $@.c $(DEPS) -I/home/josh/ccv/lib -L/home/josh/ccv/lib -lccv

cluster:
	gcc -g cluster.c -lm

$(OTHER): capture.o
	$(ENV) gcc $(CFLAGS) $^ $@.c $(DEPS)

clean:
	rm -f *.o a.out
