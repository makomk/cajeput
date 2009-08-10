#include "sl_llsd.h"

const char *test_1 = "<llsd><map><key>events</key><array><map><key>body</key><map><key>Info</key><map><key>AgentID</key><uuid>0fd0e798-a54f-40b1-8024-f7b19243d26c</uuid><key>LocationID</key><binary encoding=\"base64\">AAAAAw==</binary><key>RegionHandle</key><binary encoding=\"base64\">AAPmAAAD6AA=</binary><key>SeedCapability</key><string>https://sim7.aditi.lindenlab.com:12043/cap/e661f4ec-e8c8-477f-e1bd-bcb79bedaa24</string><key>SimAccess</key><integer>13</integer><key>SimIP</key><binary encoding=\"base64\">yA8FSA==</binary><key>SimPort</key><integer>13008</integer><key>TeleportFlags</key><binary encoding=\"base64\">AAAgEA==</binary></map></map><key>message</key><string>TeleportFinish</string></map></array><key>id</key><integer>1</integer></map></llsd>";

const char *test_2 = "<llsd><array><uuid>     0fd0e798-a54f-40b1-8024-f7b19243d26c</uuid><integer>12345</integer></array></llsd>";

const char *test_3 = "<llsd><map><key>events</key><array><map><key>body</key><map><key>Info</key><map><key>AgentID</key><uuid>0fd0e798-a54f-40b1-8024-f7b19243d26c</uuid><key>SeedCapability</key><string>https://sim7.aditi.lindenlab.com:12043/cap/e661f4ec-e8c8-477f-e1bd-bcb79bedaa24</string><key>SimAccess</key><integer>13</integer><key>SimPort</key><integer>13008</integer></map></map><key>message</key><string>TeleportFinish</string></map></array><key>id</key><integer>1</integer></map></llsd>";

int main(void) {
  char *serial;
  sl_llsd * llsd = llsd_parse_xml(test_1, strlen(test_1));
  if(llsd != NULL) {
    llsd_pretty_print(llsd, 0);
    serial = llsd_serialise_xml(llsd);
    if(serial != NULL) {
      printf("Reserialised: %s\n",serial);
    }
    llsd_free(llsd);
  }
}
