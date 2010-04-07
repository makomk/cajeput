GLIB_INCLUDES := $(shell pkg-config glib-2.0 --cflags) $(shell pkg-config libxml-2.0 --cflags) $(shell pkg-config libsoup-2.4 --cflags) $(shell pkg-config json-glib-1.0 --cflags)
GLIB_LIBS := $(shell pkg-config glib-2.0 --libs) $(shell pkg-config libxml-2.0 --libs) $(shell pkg-config libsoup-2.4 --libs) $(shell pkg-config json-glib-1.0 --libs)
# BULLET_LIBS := -lbulletdynamics -lbulletcollision -lbulletmath
BULLET_LIBS := -lBulletDynamics -lBulletCollision -lLinearMath

WARNING_OPTS=-Wall -Werror=format-security -Werror=init-self -Werror=parentheses -Werror=sequence-point
CXXFLAGS=$(WARNING_OPTS) -O -I /usr/include/bullet $(GLIB_INCLUDES)  -ggdb -DDEBUG
CFLAGS=$(WARNING_OPTS) -O -ggdb $(GLIB_INCLUDES)

# we dont bother compiling caj_llsd_test anymore; not useful
all: cajeput_sim cajeput_j2k_test lsl_compile

.PHONY: all clean depend

clean:
	rm -f cajeput_main cajeput_sim caj_llsd_test *.o sl_messages.[ch]
	rm -f lsl.tab.[ch] lsl-lex.c

depend: lsl_consts.c lsl-lex.c lsl.tab.c lsl.tab.h sl_messages.c sl_messages.h caj_version.h
	gcc -MM *.cpp *.c > depend.make

FORCE:

caj_version.h: caj_version.h.in FORCE
	./make_caj_version.sh

libopenjpeg/openjpeg.a: FORCE
	cd libopenjpeg && make

lsl-lex.c: lsl.lex lsl.tab.h
	flex -o lsl-lex.c lsl.lex

lsl.tab.c lsl.tab.h: lsl.y
	bison -d -v lsl.y

lsl.tab.o: lsl.tab.c lsl.tab.h caj_lsl_parse.h

lsl-lex.o: lsl-lex.c lsl.tab.h

caj_vm_insns.h caj_vm_ops.h: caj_vm_make_insns.py opcode_data.txt
	python caj_vm_make_insns.py

lsl_consts.c: lsl_consts.py caj_script.h
	python lsl_consts.py

lsl_compile: lsl.tab.o lsl-lex.o caj_lsl_compile.o lsl_consts.o
	$(CXX) -O0 -Wall -ggdb -o lsl_compile lsl.tab.o lsl-lex.o caj_lsl_compile.o lsl_consts.o -lfl

# cajeput_vm_test: caj_vm.o
#	$(CXX) $(CFLAGS) -o cajeput_vm_test caj_vm.o

cajeput_j2k_test: cajeput_j2k_test.c cajeput_j2k.h cajeput_j2k.o libopenjpeg/openjpeg.a
	$(CC) $(CFLAGS) -o cajeput_j2k_test cajeput_j2k_test.c cajeput_j2k.o libopenjpeg/openjpeg.a -lm

caj_llsd_test: caj_llsd.c caj_llsd.h caj_llsd_test.c 
	$(CC) $(CFLAGS) -o caj_llsd_test caj_llsd.c  caj_llsd_test.c -lxml2 -luuid -lglib-2.0

include depend.make

CAJEPUT_OBJS=opensim_robust_xml.o opensim_grid_glue.o caj_omv_udp.o cajeput_main.o cajeput_caps.o sl_messages.o sl_udp_proto.o caj_llsd.o physics_bullet.o cajeput_inventory.o cajeput_assets.o opensim_inventory_glue.o opensim_asset_glue.o opensim_xml_glue.o opensim_intersim.o cajeput_j2k.o terrain_compress.o cajeput_anims.o cajeput_evqueue.o cajeput_hooks.o caj_parse_nini.o caj_scripting.o caj_types.o caj_vm.o cajeput_dump.o cajeput_world.o cajeput_user.o libopenjpeg/openjpeg.a

cajeput_sim: $(CAJEPUT_OBJS)
	$(CXX) $(CXXFLAGS) -o cajeput_sim $(CAJEPUT_OBJS) $(GLIB_LIBS) -luuid $(BULLET_LIBS)

sl_messages.c sl_messages.h: message_template.msg msgtmpl2c.py
	python msgtmpl2c.py

