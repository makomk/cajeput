CFLAGS=-ggdb -O2

openjpeg.a: bio.o cio.o dwt.o event.o image.o j2k.o j2k_lib.o jp2.o jpt.o \
   mct.o mqc.o openjpeg.o pi.o raw.o t1.o t1_generate_luts.o t2.o tcd.o tgt.o
	$(AR) rs openjpeg.a bio.o cio.o dwt.o event.o image.o j2k.o j2k_lib.o jp2.o jpt.o mct.o mqc.o openjpeg.o pi.o raw.o t1.o t1_generate_luts.o t2.o tcd.o tgt.o

bio.o: bio.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
cio.o: cio.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
dwt.o: dwt.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
event.o: event.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
image.o: image.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
j2k.o: j2k.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
j2k_lib.o: j2k_lib.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h \
  event.h cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h \
  tcd.h t1.h dwt.h t2.h mct.h int.h fix.h
jp2.o: jp2.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
jpt.o: jpt.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
mct.o: mct.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
mqc.o: mqc.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
openjpeg.o: openjpeg.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h \
  event.h cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h \
  tcd.h t1.h dwt.h t2.h mct.h int.h fix.h
pi.o: pi.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h cio.h \
  image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h dwt.h \
  t2.h mct.h int.h fix.h
raw.o: raw.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
t1.o: t1.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h cio.h \
  image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h dwt.h \
  t2.h mct.h int.h fix.h t1_luts.h
t1_generate_luts.o: t1_generate_luts.c opj_includes.h openjpeg.h \
  j2k_lib.h opj_malloc.h event.h cio.h image.h j2k.h jp2.h jpt.h mqc.h \
  raw.h bio.h tgt.h pi.h tcd.h t1.h dwt.h t2.h mct.h int.h fix.h
t2.o: t2.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h cio.h \
  image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h dwt.h \
  t2.h mct.h int.h fix.h
tcd.o: tcd.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
tgt.o: tgt.c opj_includes.h openjpeg.h j2k_lib.h opj_malloc.h event.h \
  cio.h image.h j2k.h jp2.h jpt.h mqc.h raw.h bio.h tgt.h pi.h tcd.h t1.h \
  dwt.h t2.h mct.h int.h fix.h
