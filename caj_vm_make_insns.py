#!/usr/bin/python

# Copyright (c) 2009 Aidan Thornton, all rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AIDAN THORNTON ''AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL AIDAN THORNTON BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import re, sys

_opdata_re = re.compile(r"^(\d+) ([A-Z][A-Z_0-9]*):\s+(\w+\b)?\s*(\w+\b)?\s*->\s*(\w+\b)?\s*(?:\((\w+)\))?\s*(//.*)?$")

opdata_in = open("opcode_data.txt","r").readlines()

opdata = { }; num_opcodes = 0

for line in opdata_in:
    sline = line.strip()
    if sline == "" or sline[:2] == "//":
        continue
    
    m = _opdata_re.match(line);
    if m == None:
        print "ERROR: bad line %s" % line; sys.exit(1);

    opcode = int(m.group(1)); name = m.group(2);
    a1_type = m.group(3); a2_type = m.group(4);
    ret_type = m.group(5); special = m.group(6);
    comment = m.group(7);

    if special == None: special = "NORMAL"
    
    if comment == None:
        comment = ""
    else: comment = " " + comment
        
    if a1_type == None and a2_type != None:
        # not needed yet, but will be once we start adding operators
        a1_type = a2_type; a2_type = None

    if opcode >= num_opcodes: num_opcodes = opcode + 1

    if opdata.has_key(opcode):
        print "ERROR: duplicate def of opcode %i" % opcode
        sys.exit(0);

    opdata[opcode] = (name, a1_type, a2_type, ret_type, special, comment)

    # print "DEBUG op %i name(%s) a1(%s) a2(%s) ret(%s) special(%s)" % (opcode, name, a1_type, a2_type, ret_type, special)

hfile = open("caj_vm_insns.h","w")

hfile.write("""/* This file is auto-generated from opcode_data.txt - DO NOT MODIFY! */

/* Copyright (c) 2009 Aidan Thornton, all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AIDAN THORNTON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AIDAN THORNTON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef IN_CAJ_VM_H
#error This file should not be included directly. Include caj_vm.h instead
#endif

/* This file is auto-generated from opcode_data.txt - DO NOT MODIFY! */
""");

for opcode in range(0,num_opcodes):
    if opdata.has_key(opcode):
        (name, a1_type, a2_type, ret_type, special, comment) = opdata[opcode]
        hfile.write("#define INSN_%s %i%s\n" % (name,opcode,comment));


hfile.write("\n#define NUM_INSNS %i\n\n" % num_opcodes)

def _fix_type(vtype):
    if vtype == None: vtype = "none"
    return vtype.upper()

hfile.write("static const insn_info vm_insns[NUM_INSNS] = {\n")
for opcode in range(0,num_opcodes):
    if opdata.has_key(opcode):
        (name, a1_type, a2_type, ret_type, special, comment) = opdata[opcode]
    else:
        name = "doesn't exist"; a1_type = None; a2_type = None; ret_type = None
        special = "INVALID"
        
    hfile.write("  { IVERIFY_%s, VM_TYPE_%s, VM_TYPE_%s, VM_TYPE_%s }, // %s\n" %
                (special, _fix_type(a1_type),  _fix_type(a2_type),
                 _fix_type(ret_type), name))
hfile.write("};\n\n");

hfile.close()
