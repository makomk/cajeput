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

_opdata_re = re.compile(r"^(\d+) ([A-Z][A-Z_0-9]*):\s+([a-z_]+?\b)??\s*([^a-z_0-9 ]+(?=\s))?\s*([a-z_]+?\b)??\s*->\s*([a-z_]+\b)?\s*(?:\((\w+)\))?\s*(//.*)?$") 

opdata_in = open("opcode_data.txt","r").readlines()

opdata = { }; num_opcodes = 0

bin_op_names = { '+':'NODE_ADD', '-':'NODE_SUB', '*':'NODE_MUL', '/':'NODE_DIV',
                 "<":"NODE_LESS", ">":"NODE_GREATER", "==":"NODE_EQUAL",
                 "!=":"NODE_NEQUAL", ">=":"NODE_GEQUAL", "<=":"NODE_LEQUAL",
                 "&&":"NODE_L_AND", "||":"NODE_L_OR", "!":"NODE_L_NOT",
                 "&":"NODE_AND", "|":"NODE_OR", "~":"NODE_NOT", "^":"NODE_XOR",
                 "%":"NODE_MOD", "<<":"NODE_SHL", ">>":"NODE_SHR",
                 }

bin_ops = { }; un_ops = { } 

for line in opdata_in:
    sline = line.strip()
    if sline == "" or sline[:2] == "//":
        continue
    
    m = _opdata_re.match(line);
    if m == None:
        print "ERROR: bad line %s" % line; sys.exit(1);
    opcode = int(m.group(1)); name = m.group(2);
    a1_type = m.group(3); op_sym = m.group(4);
    a2_type = m.group(5);
    ret_type = m.group(6); special = m.group(7);
    comment = m.group(8);

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

    if op_sym != None:
        if a1_type == None:
            print "WARNING: opcode %s has symbol but no type!" % name
        elif a2_type == None:
            if not un_ops.has_key(op_sym):
                un_ops[op_sym] = { }
            if un_ops[op_sym].has_key(a1_type):
                print "ERROR: duplicate defn of op %s %s" % (op_sym, a1_type)
                sys.exit(1)
            un_ops[op_sym][a1_type] = name
        else:
             if not bin_ops.has_key(op_sym):
                 bin_ops[op_sym] = { }
             if bin_ops[op_sym].has_key((a1_type,a2_type)):
                print "ERROR: duplicate defn of op %s %s %s" % (a1_type, op_sym, a2_type)
                sys.exit(1)
             bin_ops[op_sym][(a1_type,a2_type)] = name
            

    # print "DEBUG op %i name(%s) a1(%s) sym(%s) a2(%s) ret(%s) special(%s) comment(%s)" % (opcode, name, a1_type, op_sym, a2_type, ret_type, special, comment)

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
    return "VM_TYPE_%s" % vtype.upper()

hfile.write("static const insn_info vm_insns[NUM_INSNS] = {\n")
for opcode in range(0,num_opcodes):
    if opdata.has_key(opcode):
        (name, a1_type, a2_type, ret_type, special, comment) = opdata[opcode]
    else:
        name = "doesn't exist"; a1_type = None; a2_type = None; ret_type = None
        special = "INVALID"
        
    hfile.write("  { IVERIFY_%s, %s, %s, %s }, // %s\n" %
                (special, _fix_type(a1_type),  _fix_type(a2_type),
                 _fix_type(ret_type), name))
hfile.write("};\n\n");

hfile.close()

hfile2 = open("caj_vm_ops.h","w")

hfile2.write("""/* This file is auto-generated from opcode_data.txt - DO NOT MODIFY! */

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
 
""");

hfile2.write("static uint16_t get_insn_binop(int node_type, uint8_t ltype, uint8_t rtype) {\n")
hfile2.write("  switch(node_type) {\n");
for op, insns in bin_ops.iteritems():
    if not bin_op_names.has_key(op):
        print "WARNING: no mapping for op %s, skipping" % op
        continue
    
    hfile2.write("  case %s:\n" % bin_op_names[op]);
    hfile2.write("    switch(MK_VM_TYPE_PAIR(ltype, rtype)) {\n");
    for types, insn in insns.iteritems():
        hfile2.write("    case MK_VM_TYPE_PAIR(%s, %s):\n" %
                     (_fix_type(types[0]), _fix_type(types[1])));
        hfile2.write("      return INSN_%s;\n" % insn);
    hfile2.write("    };\n");
    hfile2.write("    break;\n");
hfile2.write("  }\n  return 0;\n}\n\n");
