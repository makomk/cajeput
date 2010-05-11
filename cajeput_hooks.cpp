#include "cajeput_int.h"
#include <set>

void cajeput_add_sim_added_hook(struct simgroup_ctx *sgrp,
				sim_generic_cb cb, void *priv) {
  sgrp->sim_added_hook.add_callback(cb, priv);
}

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

void user_add_delete_hook(struct user_ctx *ctx,
			   user_generic_cb cb, void *priv) {
  ctx->delete_hook.add_callback(cb, priv);
}
void user_remove_delete_hook(struct user_ctx *ctx,
			   user_generic_cb cb, void *priv) {
  ctx->delete_hook.remove_callback(cb, priv);
}

void user_call_delete_hook(struct user_ctx *ctx) {
  for(caj_callback<user_generic_cb>::cb_set::iterator iter = 
	ctx->delete_hook.callbacks.begin(); 
      iter != ctx->delete_hook.callbacks.end(); iter++) {
    iter->cb(ctx, iter->priv);
  }
}
