ENV=LD_RUN_PATH=/home/josh/compiled/lib
CFLAGS=-Wall -Wextra -Wno-unused-function -D_GNU_SOURCE -g
DEPS=$(shell pkg-config --cflags --libs opencv libavdevice libswscale)

OTHER=test stream face histogram hc bkg patch fill kdtest gt
OBJS=encode.o capture.o wht.o gck.o select.o kdtree.o prop.o

all: gt

$(OBJS): %.o : %.h
	$(ENV) gcc $(CFLAGS) $(DEPS) -c $(OBJS:.o=.c)

motion: $(OBJS)
	$(ENV) gcc $(CFLAGS) $^ motion.c $(DEPS)

ccv: capture.o
	$(ENV) gcc $(CFLAGS) $^ $@.c $(DEPS) -I/home/josh/ccv/lib -L/home/josh/ccv/lib -lccv

cluster:
	gcc -g cluster.c -lm

$(OTHER): $(OBJS)
	$(ENV) gcc $(CFLAGS) $^ $@.c $(DEPS)

clean:
	rm -f *.o a.out
