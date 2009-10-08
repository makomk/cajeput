#!/usr/bin/python
import sys, re

class lsl_const:
    def __init__(self,vtype,sval):
        self.vtype = vtype; self.sval = sval

class lsl_vect:
    def __init__(self,x,y,z):
        self.x = x; self.y = y; self.z = z;

class lsl_rot:
    def __init__(self,x,y,z,s):
        self.x = x; self.y = y; self.z = z; self.s = s

_header_re = re.compile(r"^#define\s+(\w+)\s+(.+?)(?://.*)?$")

class c_header:
    def __init__(self, fname):
        self.names = { }
        f = open(fname, 'r');
        for line in f.readlines():
            m = _header_re.match(line)
            if m != None:
                val = m.group(2)
                self.names[m.group(1)] = val
                print "Got %s = %s" % (m.group(1), val)

    def get_int(self, name):
        return eval(self.names[name])

caj_script_h = c_header("caj_script.h")

lsl_consts = {
    "FALSE": 0,
    "TRUE": 1,
    "PI": lsl_const("float","M_PI"),
    "TWO_PI": lsl_const("float","M_PI*2"),
    "PI_BY_TWO": lsl_const("float","M_PI_2"),
    "DEBUG_CHANNEL": -2147483648,
    "ZERO_VECTOR": lsl_vect(0,0,0),
    "ZERO_ROTATION": lsl_rot(0,0,0,1),
    "NULL_KEY": "00000000-0000-0000-0000-000000000000", # yes, a string.
    "CHANGED_REGION_START": caj_script_h.get_int("CHANGED_REGION_START"),
    "LINK_SET": caj_script_h.get_int("LINK_SET")
    # TODO
}

consts_out = [ ]

for name, val in lsl_consts.iteritems():
    if isinstance(val, int):
        consts_out.append((name,"int",str(val)))
    elif isinstance(val, float):
        consts_out.append((name,"float",str(val)))
    elif isinstance(val, str):
        consts_out.append((name,"str",repr(val).replace("'",'"'))) # HACK!
    elif isinstance(val, lsl_vect):
        consts_out.append((name,"vect", "{%f, %f, %f}" % (val.x, val.y, val.z)))
    elif isinstance(val, lsl_rot):
        consts_out.append((name,"rot", "{%f, %f, %f, %f}" %
                           (val.x, val.y, val.z, val.s)))
    elif isinstance(val, lsl_const):
        consts_out.append((name,val.vtype,val.sval))
    else:
        print "ERROR: bad type of const %s" % name
        sys.exit(2)

consts_out.sort(key=lambda t: t[0])

print consts_out

cfile = open("lsl_consts.c","w");
cfile.write("""/* Automatically generated from lsl_consts.py - DO NOT EDIT */

#include <math.h>
#include <string.h>
#include "caj_lsl_parse.h"

""");

cfile.write("#define NUM_LSL_CONSTS %i\n\n" % len(consts_out))

def _fix_type(vtype):
    return "VM_TYPE_%s" % vtype.upper()

_typ_map = {"float":"f","int":"i","str":"s","vect":"v","rot":"v"}

cfile.write("/* sorted alphabetically. Do not edit it manually, though! */\n");
cfile.write("static const lsl_const consts[NUM_LSL_CONSTS] = {\n")
for const_def in consts_out:
    cfile.write("  { %s, %s, { .%s = %s } },\n" % (repr(const_def[0]).replace("'",'"'),
                _fix_type(const_def[1]), _typ_map[const_def[1]],
                const_def[2]));
# cfile.write("  { }\n"); # because I don't trust the binary search
cfile.write("};\n");

cfile.write("""
const lsl_const* find_lsl_const(const char* name) {
  int left = 0, right = NUM_LSL_CONSTS;
  while(left < right) {
    int mid = left+((right-left)/2);
    int dirn = strcmp(name, consts[mid].name);
    if(dirn == 0) return &consts[mid];
    if(dirn < 0) {
       right = mid;
    } else {
       left = mid+1;
    }
  }
  return 0;
}
""");
