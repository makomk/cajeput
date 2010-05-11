/* Copyright (c) 2009-2010 Aidan Thornton, all rights reserved.
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

#include "caj_llsd.h"

const char *test_1 = "<llsd><map><key>events</key><array><map><key>body</key><map><key>Info</key><map><key>AgentID</key><uuid>0fd0e798-a54f-40b1-8024-f7b19243d26c</uuid><key>LocationID</key><binary encoding=\"base64\">AAAAAw==</binary><key>RegionHandle</key><binary encoding=\"base64\">AAPmAAAD6AA=</binary><key>SeedCapability</key><string>https://sim7.aditi.lindenlab.com:12043/cap/e661f4ec-e8c8-477f-e1bd-bcb79bedaa24</string><key>SimAccess</key><integer>13</integer><key>SimIP</key><binary encoding=\"base64\">yA8FSA==</binary><key>SimPort</key><integer>13008</integer><key>TeleportFlags</key><binary encoding=\"base64\">AAAgEA==</binary></map></map><key>message</key><string>TeleportFinish</string></map></array><key>id</key><integer>1</integer></map></llsd>";

const char *test_2 = "<llsd><array><uuid>     0fd0e798-a54f-40b1-8024-f7b19243d26c</uuid><integer>12345</integer></array></llsd>";

const char *test_3 = "<llsd><map><key>events</key><array><map><key>body</key><map><key>Info</key><map><key>AgentID</key><uuid>0fd0e798-a54f-40b1-8024-f7b19243d26c</uuid><key>SeedCapability</key><string>https://sim7.aditi.lindenlab.com:12043/cap/e661f4ec-e8c8-477f-e1bd-bcb79bedaa24</string><key>SimAccess</key><integer>13</integer><key>SimPort</key><integer>13008</integer></map></map><key>message</key><string>TeleportFinish</string></map></array><key>id</key><integer>1</integer></map></llsd>";

int main(void) {
  char *serial;
  caj_llsd * llsd = llsd_parse_xml(test_1, strlen(test_1));
  if(llsd != NULL) {
    llsd_pretty_print(llsd, 0);
    serial = llsd_serialise_xml(llsd);
    if(serial != NULL) {
      printf("Reserialised: %s\n",serial);
    }
    llsd_free(llsd);
  }
}
