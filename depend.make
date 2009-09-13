cajeput_dump.o: cajeput_dump.cpp cajeput_core.h caj_types.h \
  cajeput_world.h cajeput_int.h caj_llsd.h cajeput_user.h
cajeput_evqueue.o: cajeput_evqueue.cpp caj_types.h caj_llsd.h \
  cajeput_core.h cajeput_int.h cajeput_world.h cajeput_user.h
cajeput_hooks.o: cajeput_hooks.cpp cajeput_int.h caj_llsd.h caj_types.h \
  cajeput_core.h cajeput_world.h cajeput_user.h
cajeput_inventory.o: cajeput_inventory.cpp cajeput_core.h caj_types.h \
  cajeput_user.h
cajeput_main.o: cajeput_main.cpp caj_llsd.h caj_types.h cajeput_core.h \
  cajeput_int.h cajeput_world.h cajeput_user.h cajeput_j2k.h caj_script.h \
  terrain_compress.h caj_parse_nini.h
cajeput_user.o: cajeput_user.cpp cajeput_core.h caj_types.h cajeput_int.h \
  caj_llsd.h cajeput_world.h cajeput_user.h cajeput_anims.h caj_helpers.h \
  opensim_xml_glue.h
cajeput_world.o: cajeput_world.cpp cajeput_core.h caj_types.h \
  cajeput_world.h cajeput_int.h caj_llsd.h cajeput_user.h cajeput_prim.h \
  caj_script.h
caj_lsl_compile.o: caj_lsl_compile.cpp caj_lsl_parse.h caj_vm.h \
  caj_types.h caj_vm_insns.h caj_vm_asm.h caj_vm_internal.h caj_vm_ops.h
caj_omv_udp.o: caj_omv_udp.cpp sl_messages.h caj_types.h sl_udp_proto.h \
  cajeput_core.h cajeput_int.h caj_llsd.h cajeput_world.h cajeput_user.h \
  cajeput_anims.h cajeput_prim.h caj_helpers.h caj_omv.h \
  terrain_compress.h
caj_scripting.o: caj_scripting.cpp cajeput_core.h caj_types.h \
  cajeput_world.h cajeput_user.h caj_vm.h caj_vm_insns.h caj_version.h \
  caj_script.h
caj_types.o: caj_types.cpp caj_types.h
caj_vm.o: caj_vm.cpp caj_vm.h caj_types.h caj_vm_insns.h \
  caj_vm_internal.h
opensim_grid_glue.o: opensim_grid_glue.cpp cajeput_core.h caj_types.h \
  cajeput_user.h opensim_grid_glue.h opensim_xml_glue.h
opensim_intersim.o: opensim_intersim.cpp cajeput_core.h caj_types.h \
  cajeput_user.h opensim_grid_glue.h
opensim_inventory_glue.o: opensim_inventory_glue.cpp cajeput_core.h \
  caj_types.h cajeput_user.h opensim_grid_glue.h opensim_xml_glue.h
opensim_xml_glue.o: opensim_xml_glue.cpp opensim_xml_glue.h caj_types.h
physics_bullet.o: physics_bullet.cpp cajeput_core.h caj_types.h \
  cajeput_world.h cajeput_prim.h
cajeput_anims.o: cajeput_anims.c
cajeput_j2k.o: cajeput_j2k.c cajeput_j2k.h libopenjpeg/openjpeg.h
cajeput_j2k_test.o: cajeput_j2k_test.c cajeput_j2k.h
caj_llsd.o: caj_llsd.c caj_llsd.h caj_types.h
caj_llsd_test.o: caj_llsd_test.c caj_llsd.h caj_types.h
caj_parse_nini.o: caj_parse_nini.c caj_parse_nini.h
lsl_consts.o: lsl_consts.c caj_lsl_parse.h
lsl-lex.o: lsl-lex.c caj_lsl_parse.h lsl.tab.h
lsl.tab.o: lsl.tab.c caj_lsl_parse.h
sl_messages.o: sl_messages.c sl_messages.h caj_types.h
sl_udp_proto.o: sl_udp_proto.c sl_messages.h caj_types.h sl_udp_proto.h \
  caj_helpers.h
terrain_compress.o: terrain_compress.c caj_types.h caj_helpers.h \
  terrain_compress.h
