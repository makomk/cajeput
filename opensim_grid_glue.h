#ifndef OPENSIM_GRID_GLUE_H
#define OPENSIM_GRID_GLUE_H

struct grid_glue_ctx {
  gchar *userserver, *gridserver, *assetserver;
  gchar *inventoryserver;
  gchar *user_recvkey, *asset_recvkey, *grid_recvkey;
  gchar *user_sendkey, *asset_sendkey, *grid_sendkey;
  uuid_t region_secret;
};

struct user_grid_glue {
  int refcnt;
  user_ctx *ctx;
};
void user_grid_glue_ref(user_grid_glue *user_glue);
void user_grid_glue_deref(user_grid_glue *user_glue);

#define GRID_PRIV_DEF(sim) struct grid_glue_ctx* grid = (struct grid_glue_ctx*) sim_get_grid_priv(sim);
#define USER_PRIV_DEF(priv) struct user_grid_glue* user_glue = (struct user_grid_glue*) (priv);
#define USER_PRIV_DEF2(sim) struct user_grid_glue* user_glue = (struct user_grid_glue*) user_get_grid_priv(sim);

void fetch_user_inventory(simulator_ctx *sim, user_ctx *user,
			  void *user_priv);

#endif
