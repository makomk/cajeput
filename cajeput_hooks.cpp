#include "cajeput_int.h"
#include <set>


void sim_add_shutdown_hook(struct simulator_ctx *sim,
			   sim_generic_cb cb, void *priv) {
  sim->shutdown_hook.add_callback(cb, priv);
}

void sim_remove_shutdown_hook(struct simulator_ctx *sim,
			      sim_generic_cb cb, void *priv) {
  sim->shutdown_hook.remove_callback(cb, priv);
}

void sim_call_shutdown_hook(struct simulator_ctx *sim) {
  for(caj_callback<sim_generic_cb>::cb_set::iterator iter = 
	sim->shutdown_hook.callbacks.begin(); 
      iter != sim->shutdown_hook.callbacks.end(); iter++) {
    iter->cb(sim, iter->priv);
  }
}
