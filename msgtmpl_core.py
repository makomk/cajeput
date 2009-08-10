# msgtmpl_core.py: gutted version of the Twisl message template parser, with
# all the message packing and unpacking parts removed, for Cajeput

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

from struct import pack, unpack
from new import instancemethod
from cStringIO import StringIO
import codecs

utf8encode = codecs.getencoder('utf-8')

class _TmplTokenizer:
    def __init__(self, f):
        self.f = f
        self.line = [ ]

    def peek(self):
        while True:
            if len(self.line) != 0 and self.line[0][0:2] != "//":
                return self.line[0]

            t = self.f.readline();
            if t == '':
                self.line = [""]
            else:
                self.line = t.strip().split()
            
    def get(self):
        if self.peek() == '':
            raise TemplateParseException("Unexpected EOF")
        return self.line.pop(0)

    def getint(self):
        t = self.get()
        try:
            if t[0:2] == '0x':
                return int(t[2:],16)
            else:
                return int(t)
        except ValueError:
            raise TemplateParseException("Expected an integer, got " +t)

    def want(self,*what):
        if self.peek() in what:
            return self.line.pop(0)
        else:
            return None

    def need(self,*what):
        if self.peek() in what:
            return self.line.pop(0)
        else:
            raise TemplateParseException("Expected "+("/".join(what))+", got "+self.peek())

class TemplateParseException(Exception):
    #def __init__(self, info):
    #    Exception.__init__(self,info)
    pass

_FREQ_LOW = 1
_FREQ_MED = 2
_FREQ_HIGH = 3
_freqnames = {_FREQ_LOW:'Low',_FREQ_MED:'Medium',_FREQ_HIGH:'High'}
_freqids = {'Low':_FREQ_LOW, 'Medium':_FREQ_MED, 'High':_FREQ_HIGH,
            'Fixed':_FREQ_LOW}

MSG_UNDEPRECATED = 0
MSG_UDP_DEPRECATED = 1
MSG_UDP_BLACKLISTED = 2
MSG_DEPRECATED = 3


class _TmplMessage:
    def __str__(self):
        ret = "Message %s (%s %i) " % (self.name, _freqnames[self.freq], self.number)
        if self.trusted:
            ret += "Trusted "
        else:
            ret += "NotTrusted "

        if self.zerocoded:
            ret += 'Zerocoded\n'
        else:
            ret += 'Unencoded\n'

        return ret + "\n".join([str(b) for b in self.blocks])

class _TmplBlock:
    __slots__ = ('name','count','fields','fields_map')
    
    def __str__(self):
        if self.count == 1:
            ret = "  Block %s: Single\n" % self.name
        elif self.count == None:
            ret = "  Block %s: Variable\n" % self.name
        else:
            ret = "  Block %s: Multiple %i\n" % (self.name, self.count)

        return ret + "\n".join([str(f) for f in self.fields])

class _TmplField:
    __slots__ = ('name','type','fulltype','size')
    
    def __str__(self):
        if self.type in ('Variable', 'Fixed'):
            return "    Field %s: %s %i" % (self.name, self.type, self.size)
        else:
            return "    Field %s: %s" % (self.name, self.type)

class MessageTemplate:
    def __init__(self,f):
        tok = _TmplTokenizer(f)
        tok.need("version")
        self.version = tok.get()
        self.msgs = { }
        self.low_msgs = { }; self.med_msgs = { }; self.high_msgs = { }

        while tok.need("{","") == '{':
            msg = _TmplMessage()
            msg.name = tok.get()
            self.msgs[msg.name] = msg
            msg.freq = _freqids[tok.need("Low","Medium","High","Fixed")]
            msg.number = tok.getint() & 0xffff # Linden Labs are evil.
            if msg.freq == _FREQ_LOW:
                self.low_msgs[msg.number] = msg
                msg.packedid = '\xff\xff' + pack('>H',msg.number)
            elif msg.freq == _FREQ_MED:
                self.med_msgs[msg.number] = msg
                msg.packedid = '\xff' + pack('>B',msg.number)
            elif msg.freq == _FREQ_HIGH:
                self.high_msgs[msg.number] = msg
                msg.packedid = pack('>B',msg.number)
            else:
                raise ValueError('Frequency is %s?!' % msg.freq)
            msg.trusted = (tok.need("Trusted","NotTrusted") == "Trusted")
            msg.zerocoded = (tok.need("Zerocoded","Unencoded") == "Zerocoded")
            depr = tok.want("UDPDeprecated","UDPBlackListed","Deprecated");
            if depr == None:
                msg.deprecated = MSG_UNDEPRECATED
            elif depr == "UDPDeprecated":
                msg.deprecated = MSG_UDP_DEPRECATED
            elif depr == "UDPBlackListed":
                msg.deprecated = MSG_UDP_BLACKLISTED
            elif depr == "Deprecated":
                msg.deprecated = MSG_DEPRECATED
            msg.blocks = [ ]; msg.blocks_map = { }
            
            while  tok.need("{","}") == '{':
                block = _TmplBlock()
                block.name = tok.get();
                blocktype = tok.need("Single","Multiple","Variable")
                if blocktype == 'Single':
                    block.count = 1
                elif blocktype == 'Multiple':
                    block.count = tok.getint()
                elif blocktype == 'Variable':
                    block.count = None

                block.fields = [ ]; block.fields_map = { }
                msg.blocks.append(block)
                msg.blocks_map[block.name] = block

                while tok.need("{","}") == '{':
                    field = _TmplField()
                    field.name = tok.get()
                    field.type = tok.need('U8','U16',"U32",'U64','S8','S16','S32','S64','BOOL','LLUUID','IPADDR','IPPORT','Variable','Fixed','LLVector3','LLQuaternion','F32','F64','LLVector3d','LLVector4')
                    field.fulltype = field.type
                    if field.type in ('Variable', 'Fixed'):
                        field.size = tok.getint()
                        if field.fulltype == 'Variable':
                            field.fulltype += " " + str(field.size)
                    block.fields.append(field)
                    block.fields_map[field.name]=field

                    tok.need("}")
                    
            # print str(msg)

    def getMessageName(self,data):
        if data[0] == '\xff':
            if data[1] == '\xff':
                return self.low_msgs[unpack('>H',data[2:4])[0]].name
            else:
                return  self.med_msgs[unpack('>B',data[1])[0]].name
        else:
            return self.high_msgs[unpack('>B',data[0])[0]].name



if __name__=='__main__':
    tmpl = MessageTemplate(file('message_template.msg','r'))
