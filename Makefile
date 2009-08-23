CXXFLAGS=-Wall -I /usr/include/libxml2 -I /usr/include/glib-2.0 -I /usr/include/libsoup-2.4 -I /usr/lib64/glib-2.0/include -I /usr/include/bullet -I /usr/include/json-glib-1.0 -ggdb -DDEBUG
CFLAGS=-Wall -ggdb -I /usr/include/libxml2 -I /usr/include/glib-2.0 -I /usr/lib64/glib-2.0/include

all: cajeput_sim  sl_llsd_test cajeput_j2k_test

clean:
	rm -f cajeput_main cajeput_sim sl_llsd_test *.o sl_messages.[ch]


.DUMMY:

libopenjpeg/openjpeg.a: .DUMMY
	cd libopenjpeg && make

cajeput_j2k_test: cajeput_j2k_test.c cajeput_j2k.h cajeput_j2k.o libopenjpeg/openjpeg.a
	$(CC) $(CFLAGS) -o cajeput_j2k_test cajeput_j2k_test.c cajeput_j2k.o libopenjpeg/openjpeg.a -lm

sl_llsd_test: sl_llsd.c sl_llsd.h sl_llsd_test.c 
	$(CC) $(CFLAGS) -o sl_llsd_test sl_llsd.c  sl_llsd_test.c -lxml2 -luuid -lglib-2.0

cajeput_evqueue.o: cajeput_evqueue.cpp sl_types.h sl_llsd.h \
  cajeput_core.h cajeput_int.h
cajeput_hooks.o: cajeput_hooks.cpp cajeput_int.h sl_llsd.h sl_types.h \
  cajeput_core.h
cajeput_inventory.o: cajeput_inventory.cpp sl_messages.h sl_types.h \
  cajeput_core.h cajeput_int.h sl_llsd.h
cajeput_main.o: cajeput_main.cpp sl_messages.h sl_types.h sl_llsd.h \
  cajeput_core.h cajeput_int.h cajeput_j2k.h cajeput_prim.h \
  cajeput_anims.h terrain_compress.h
caj_omv_udp.o: caj_omv_udp.cpp sl_messages.h sl_types.h sl_udp_proto.h \
  cajeput_core.h cajeput_int.h sl_llsd.h cajeput_anims.h caj_omv.h \
  terrain_compress.h
opensim_grid_glue.o: opensim_grid_glue.cpp cajeput_core.h sl_types.h \
  opensim_grid_glue.h
opensim_intersim.o: opensim_intersim.cpp cajeput_core.h sl_types.h \
  opensim_grid_glue.h
opensim_inventory_glue.o: opensim_inventory_glue.cpp cajeput_core.h \
  sl_types.h opensim_grid_glue.h
physics_bullet.o: physics_bullet.cpp cajeput_core.h sl_types.h
cajeput_anims.o: cajeput_anims.c
cajeput_j2k.o: cajeput_j2k.c cajeput_j2k.h libopenjpeg/openjpeg.h
cajeput_j2k_test.o: cajeput_j2k_test.c cajeput_j2k.h
sl_llsd.o: sl_llsd.c sl_llsd.h sl_types.h
sl_llsd_test.o: sl_llsd_test.c sl_llsd.h sl_types.h
sl_messages.o: sl_messages.c sl_messages.h sl_types.h
sl_udp_proto.o: sl_udp_proto.c sl_messages.h sl_types.h sl_udp_proto.h
terrain_compress.o: terrain_compress.c sl_types.h terrain_compress.h

CAJEPUT_OBJS=opensim_grid_glue.o caj_omv_udp.o cajeput_main.o sl_messages.o sl_udp_proto.o sl_llsd.o physics_bullet.o cajeput_inventory.o opensim_inventory_glue.o opensim_intersim.o cajeput_j2k.o terrain_compress.o cajeput_anims.o cajeput_evqueue.o cajeput_hooks.o  libopenjpeg/openjpeg.a

cajeput_sim: $(CAJEPUT_OBJS)
	$(CXX) $(CXXFLAGS) -o cajeput_sim $(CAJEPUT_OBJS) -luuid -lglib-2.0 -lsoup-2.4 -lxml2 -lbulletdynamics -lbulletcollision -lbulletmath -ljson-glib-1.0

sl_messages.c sl_messages.h: message_template.msg msgtmpl2c.py
	python msgtmpl2c.py

