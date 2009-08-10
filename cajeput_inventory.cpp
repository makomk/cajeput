#include "sl_messages.h"
//#include "sl_llsd.h"
#include "cajeput_core.h"
#include "cajeput_udp.h"
#include "cajeput_int.h"
#include <stdlib.h>
#include <cassert>
#include <set>


struct inventory_folder {
  inventory_folder *parent;
  uuid_t folder_id;
};
