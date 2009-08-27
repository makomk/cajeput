CXXFLAGS=-Wall -I /usr/include/libxml2 -I /usr/include/glib-2.0 -I /usr/include/libsoup-2.4 -I /usr/lib64/glib-2.0/include -I /usr/include/bullet -I /usr/include/json-glib-1.0 -ggdb -DDEBUG
CFLAGS=-Wall -ggdb -I /usr/include/libxml2 -I /usr/include/glib-2.0 -I /usr/lib64/glib-2.0/include

all: cajeput_sim  sl_llsd_test cajeput_j2k_test cajeput_vm_test lsl_compile

.PHONY: all clean depend

clean:
	rm -f cajeput_main cajeput_sim sl_llsd_test *.o sl_messages.[ch]

depend:
	gcc -MM *.cpp *.c > depend.make

FORCE:

libopenjpeg/openjpeg.a: FORCE
	cd libopenjpeg && make

lsl-lex.c: lsl.lex
	flex -o lsl-lex.c lsl.lex

lsl.tab.c lsl.tab.h: lsl.y
	bison -d -v lsl.y

lsl.tab.o: lsl.tab.c lsl.tab.h caj_lsl_parse.h

lsl-lex.o: lsl-lex.c lsl.tab.h

lsl_compile: lsl.tab.o lsl-lex.o caj_lsl_compile.o
	$(CXX) -Wall -ggdb -o lsl_compile lsl.tab.o lsl-lex.o caj_lsl_compile.o -lfl

cajeput_vm_test: caj_vm.o
	$(CXX) $(CFLAGS) -o cajeput_vm_test caj_vm.o

cajeput_j2k_test: cajeput_j2k_test.c cajeput_j2k.h cajeput_j2k.o libopenjpeg/openjpeg.a
	$(CC) $(CFLAGS) -o cajeput_j2k_test cajeput_j2k_test.c cajeput_j2k.o libopenjpeg/openjpeg.a -lm

sl_llsd_test: sl_llsd.c sl_llsd.h sl_llsd_test.c 
	$(CC) $(CFLAGS) -o sl_llsd_test sl_llsd.c  sl_llsd_test.c -lxml2 -luuid -lglib-2.0

include depend.make

CAJEPUT_OBJS=opensim_grid_glue.o caj_omv_udp.o cajeput_main.o sl_messages.o sl_udp_proto.o sl_llsd.o physics_bullet.o cajeput_inventory.o opensim_inventory_glue.o opensim_intersim.o cajeput_j2k.o terrain_compress.o cajeput_anims.o cajeput_evqueue.o cajeput_hooks.o caj_parse_nini.o libopenjpeg/openjpeg.a

cajeput_sim: $(CAJEPUT_OBJS)
	$(CXX) $(CXXFLAGS) -o cajeput_sim $(CAJEPUT_OBJS) -luuid -lglib-2.0 -lsoup-2.4 -lxml2 -lbulletdynamics -lbulletcollision -lbulletmath -ljson-glib-1.0

sl_messages.c sl_messages.h: message_template.msg msgtmpl2c.py
	python msgtmpl2c.py

