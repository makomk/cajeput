cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

include(FindPkgConfig)

pkg_check_modules (GLIB2 REQUIRED  glib-2.0)
pkg_check_modules (LIBXML2 REQUIRED libxml-2.0)
pkg_check_modules (LIBSOUP REQUIRED libsoup-2.4)
pkg_check_modules (JSONGLIB REQUIRED json-glib-1.0)
pkg_check_modules (SQLITE3 REQUIRED sqlite3>=3.6.19)
#pkg_check_modules (BULLET REQUIRED bullet)

set(BULLET_INCLUDE_DIRS bullet/src)
set(BULLET_LIBRARIES BulletDynamics BulletCollision LinearMath)

include_directories(${GLIB2_INCLUDE_DIRS} ${LIBXML2_INCLUDE_DIRS} ${LIBSOUP_INCLUDE_DIRS} ${JSONGLIB_INCLUDE_DIRS} ${BULLET_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS})

add_library(openjpeg STATIC libopenjpeg/bio.c libopenjpeg/cio.c libopenjpeg/dwt.c libopenjpeg/event.c libopenjpeg/image.c libopenjpeg/j2k.c libopenjpeg/j2k_lib.c libopenjpeg/jp2.c libopenjpeg/jpt.c libopenjpeg/mct.c libopenjpeg/mqc.c libopenjpeg/openjpeg.c libopenjpeg/pi.c libopenjpeg/raw.c libopenjpeg/t1.c libopenjpeg/t1_generate_luts.c libopenjpeg/t2.c libopenjpeg/tcd.c libopenjpeg/tgt.c)

# FIXME this is broken
add_custom_target(make_caj_version.h ALL COMMAND ./make_caj_version.sh DEPENDS caj_version.h.in)

add_executable(cajeput_sim cajeput_main.cpp cajeput_caps.cpp caj_llsd.c physics_bullet.cpp cajeput_inventory.cpp cajeput_assets.cpp opensim_xml_glue.cpp cajeput_j2k.c terrain_compress.c cajeput_anims.c cajeput_evqueue.cpp cajeput_hooks.cpp caj_parse_nini.c caj_scripting.cpp caj_types.cpp caj_vm.cpp cajeput_dump.cpp cajeput_world.cpp cajeput_user.cpp caj_vm_insns.h)
add_dependencies(cajeput_sim make_caj_version.h)

target_link_libraries(cajeput_sim ${GLIB2_LIBRARIES} ${LIBXML2_LIBRARIES} ${LIBSOUP_LIBRARIES} ${BULLET_LIBRARIES} uuid openjpeg)

add_library(grid_opensim MODULE opensim_grid_glue.cpp opensim_robust_xml.cpp opensim_inventory_glue.cpp opensim_asset_glue.cpp opensim_intersim.cpp)
target_link_libraries(grid_opensim ${JSONGLIB_LIBRARIES})
add_library(grid_standalone MODULE standalone_grid_plugin.cpp)
target_link_libraries(grid_standalone ${SQLITE3_LIBRARIES})


add_library(caj_omv_udp MODULE caj_omv_udp.cpp sl_messages.c sl_udp_proto.c sl_messages.h)

add_custom_command(OUTPUT sl_messages.c sl_messages.h COMMAND python ARGS msgtmpl2c.py DEPENDS message_template.msg msgtmpl2c.py)
add_custom_command(OUTPUT caj_vm_insns.h caj_vm_ops.h COMMAND python caj_vm_make_insns.py DEPENDS caj_vm_make_insns.py opcode_data.txt)

add_executable(lsl_compile lsl.tab.c lsl-lex.c caj_lsl_compile.cpp lsl_consts.c caj_vm_insns.h caj_vm_ops.h)

add_custom_command(OUTPUT lsl.tab.c lsl.tab.h COMMAND bison -d -v lsl.y DEPENDS lsl.y)
add_custom_command(OUTPUT lsl-lex.c COMMAND flex -o lsl-lex.c lsl.lex DEPENDS lsl.lex lsl.tab.h)
add_custom_command(OUTPUT lsl_consts.c COMMAND python lsl_consts.py DEPENDS lsl_consts.py caj_script.h)

add_subdirectory(bullet)
