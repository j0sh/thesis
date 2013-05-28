default: gt

CFLAGS=-I"`ocamlc -where`" `pkg-config --cflags libavformat`
LIBS=`pkg-config --libs libavformat libswscale`

libav.o: libav.c
	gcc -c $(CFLAGS) $< $(LIBS)

libav.a : libav.o
	ocamlmklib -o libav $< $(LIBS)

gt: libav.a
	ocamlfind ocamlopt graphics.cmxa libav.mli -o $@ gt.ml -cclib "-L. -llibav $(LIBS)"

clean:
	rm -f *.[oa] *.so *.cm[ixoa] *.cmxa test
