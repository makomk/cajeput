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

#include "cajeput_core.h" // for sim_get_heightfield
#include "cajeput_world.h"
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <stdio.h> /* for debugging */
#include <unistd.h>
#include <set>
#include <vector>

struct phys_obj;

struct physics_ctx {
  simulator_ctx *sim;

  int pipe[2];
  GStaticMutex mutex;
  GThread *thread;
  GIOChannel *pipein;

  btDefaultCollisionConfiguration* collisionConfiguration;
  btCollisionDispatcher* dispatcher;
  bt32BitAxisSweep3* overlappingPairCache;
  btSequentialImpulseConstraintSolver* solver;
  btDiscreteDynamicsWorld* dynamicsWorld;

  btCollisionShape* ground_shape;
  btStaticPlaneShape *plane_0x;
  btStaticPlaneShape *plane_1x;
  btStaticPlaneShape *plane_0y;
  btStaticPlaneShape *plane_1y;

  // protected by mutex
  std::set<phys_obj*> avatars;
  std::set<phys_obj*> changed;
  int shutdown;
};

struct phys_obj {
  // most of this is protected by the mutex
  btCollisionShape *shape, *newshape;
  btRigidBody* body;
  btVector3 target_velocity;
  btVector3 pos, velocity;
  btQuaternion rot;
  int is_flying; // for avatars only
  int is_deleted;
  int pos_update;
  int objtype;
  world_obj *obj; // main thread use only
#if 0
  btPairCachingGhostObject* ghost;
  btKinematicCharacterController* control;
#endif
};

#define MAX_OBJECTS 15000

#define COL_NONE 0
#define COL_GROUND 0x1
#define COL_PRIM 0x2
#define COL_PHYS_PRIM 0x4
#define COL_AVATAR 0x8
#define COL_BORDER 0x10 // sim borders

// This is interesting. To effectively block inter-penetration, it appears each
// object must collide with the other - even if only one is physical

// what object types avatars collide with
#define AVATAR_COLLIDES_WITH (COL_GROUND|COL_PRIM|COL_PHYS_PRIM|COL_AVATAR|COL_BORDER) 

// for now, static prims don't collide with other static prims. This may change,
// though
#define PRIM_COLLIDES_WITH (COL_AVATAR|COL_PHYS_PRIM)
#define GROUND_COLLIDES_WITH (COL_AVATAR|COL_PHYS_PRIM)
#define BORDER_COLLIDES_WITH COL_AVATAR

static void add_object(struct simulator_ctx *sim, void *priv,
		       struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->type == OBJ_TYPE_AVATAR) {
    struct phys_obj *physobj = new phys_obj();
    obj->phys = physobj;
    physobj->shape = new btCapsuleShape(0.25f,1.25f); /* radius, height */
    physobj->body = NULL; physobj->newshape = NULL;
    physobj->pos = btVector3(obj->pos.x,obj->pos.z,obj->pos.y);  
    physobj->rot = btQuaternion(0.0,0.0,0.0,1.0);
    physobj->target_velocity = btVector3(0,0,0);
    physobj->velocity = btVector3(0,0,0);
    physobj->is_flying = 1; // probably the safe assumption. FIXME - do right.
    physobj->is_deleted = 0; physobj->pos_update = 0;
    physobj->obj = obj;
    physobj->objtype = OBJ_TYPE_AVATAR;

    g_static_mutex_lock(&phys->mutex);
    phys->avatars.insert(physobj);
    phys->changed.insert(physobj);
    g_static_mutex_unlock(&phys->mutex);
  } else if(obj->type == OBJ_TYPE_PRIM) {
    struct phys_obj *physobj = new phys_obj();
    obj->phys = physobj;
    physobj->shape = new btBoxShape(btVector3(obj->scale.x/2.0f, obj->scale.z/2.0f, obj->scale.y/2.0f));
    physobj->body = NULL; physobj->newshape = NULL;
    physobj->pos = btVector3(obj->pos.x,obj->pos.z,obj->pos.y);
    physobj->rot = btQuaternion(obj->rot.x,obj->rot.z,obj->rot.y,obj->rot.w);
    printf("DEBUG: object rotation <%f,%f,%f,%f>\n",obj->rot.x,obj->rot.y,obj->rot.z,obj->rot.w);
    physobj->target_velocity = btVector3(0,0,0);
    physobj->velocity = btVector3(0,0,0);
    physobj->is_deleted = 0; physobj->pos_update = 0;
    physobj->obj = obj;
    physobj->objtype = OBJ_TYPE_PRIM;

    g_static_mutex_lock(&phys->mutex);
    phys->changed.insert(physobj);
    g_static_mutex_unlock(&phys->mutex);
  } else {
    obj->phys = NULL;
  }
}

static void upd_object_pos(struct simulator_ctx *sim, void *priv,
			   struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->phys != NULL) {
    struct phys_obj *physobj = (struct phys_obj *)obj->phys;
    g_static_mutex_lock(&phys->mutex);
    physobj->pos = btVector3(obj->pos.x,obj->pos.z,obj->pos.y); 
    physobj->rot = btQuaternion(obj->rot.x,obj->rot.z,obj->rot.y,obj->rot.w);
    printf("DEBUG: object rotation <%f,%f,%f,%f>\n",obj->rot.x,obj->rot.y,obj->rot.z,obj->rot.w);
    physobj->pos_update = 1;
    phys->changed.insert(physobj);
    g_static_mutex_unlock(&phys->mutex);
  }
}


static void upd_object_full(struct simulator_ctx *sim, void *priv,
			    struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->phys != NULL && obj->type == OBJ_TYPE_PRIM) {
    struct phys_obj *physobj = (struct phys_obj *)obj->phys;
    g_static_mutex_lock(&phys->mutex);
    delete physobj->newshape;
    physobj->newshape = new btBoxShape(btVector3(obj->scale.x/2.0f, obj->scale.z/2.0f, obj->scale.y/2.0f));
    physobj->pos = btVector3(obj->pos.x,obj->pos.z,obj->pos.y);  // don't always want this, but...
    physobj->rot = btQuaternion(obj->rot.x,obj->rot.z,obj->rot.y,obj->rot.w);
    physobj->pos_update = 0;
    phys->changed.insert(physobj);
    g_static_mutex_unlock(&phys->mutex);

  }
}

static void del_object(struct simulator_ctx *sim, void *priv,
		       struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->phys != NULL) {
    struct phys_obj *physobj = (struct phys_obj *)obj->phys;
    g_static_mutex_lock(&phys->mutex);
    physobj->is_deleted = 1; physobj->obj = NULL;
    phys->changed.insert(physobj);
    if(physobj->newshape != NULL) {
      delete physobj->shape; physobj->newshape = NULL;
    }
    g_static_mutex_unlock(&phys->mutex);
    obj->phys = NULL;
  }
}

/* FIXME - HACK */
static void set_force(struct simulator_ctx *sim, void *priv,
		      struct world_obj *obj, caj_vector3 force) {
#if 0 // FIXME - need to reimplement this
  btVector3 real_force(force.x,force.z,force.y);
  if(obj->phys == NULL)
    return;
  struct phys_obj *physobj = (struct phys_obj *)obj->phys;
  //printf("DEBUG: applying force %f, %f, %f\n",force.x,force.y,force.z);
  physobj->body->clearForces(); /* FIXME - this resets rotation too! */
  physobj->body->applyCentralForce(real_force);
  if(force.x != 0.0f || force.y != 0.0f || force.z != 0.0f)
    physobj->body->setActivationState(ACTIVE_TAG);
#endif
}

static void set_target_velocity(struct simulator_ctx *sim, void *priv,
		      struct world_obj *obj, caj_vector3 velocity) {
  if(obj->phys == NULL)
    return;

  struct physics_ctx *phys = (struct physics_ctx*)priv;
  struct phys_obj *physobj = (struct phys_obj *)obj->phys;

  g_static_mutex_lock(&phys->mutex);
  physobj->target_velocity = btVector3(velocity.x, velocity.z, velocity.y);
  // phys->changed.insert(physobj); // not needed, I think.
  g_static_mutex_unlock(&phys->mutex);
}

static void set_avatar_flying(struct simulator_ctx *sim, void *priv,
			      struct world_obj *obj, int is_flying) {
  if(obj->phys == NULL)
    return;
  struct phys_obj *physobj = (struct phys_obj *)obj->phys;
  struct physics_ctx *phys = (struct physics_ctx*)priv;

  g_static_mutex_lock(&phys->mutex); // FIXME - do we really need this?
  if(physobj->is_flying != is_flying) {
    physobj->is_flying = is_flying;
#if 0 // FIXME
    physobj->body->applyCentralImpulse(btVector3(0.0f,-40.0f,0.0f)); // HACK
    physobj->body->setActivationState(ACTIVE_TAG); // so we fall properly
#endif
  }
  g_static_mutex_unlock(&phys->mutex);

  // FIXME - should make av buoyant
}

// runs on physics thread
static void do_phys_updates_locked(struct physics_ctx *phys) {

  for(std::set<phys_obj*>::iterator iter = phys->changed.begin(); 
      iter != phys->changed.end(); iter++) {
    struct phys_obj *physobj = *iter;
    if(physobj->is_deleted) {
      struct phys_obj *physobj = *iter;
      
      if(physobj->body != NULL) {
	phys->dynamicsWorld->removeCollisionObject(physobj->body);
	if(physobj->body->getMotionState()) {
	  delete physobj->body->getMotionState();
	}
	delete physobj->body;
      }
      delete physobj->shape;
      delete physobj;

      phys->avatars.erase(physobj); // should really only do this for avatars
    } else if(physobj->body == NULL || physobj->newshape != NULL) {
      assert(physobj->shape != NULL);
      if(physobj->body != NULL) {
	phys->dynamicsWorld->removeCollisionObject(physobj->body);
	if(physobj->body->getMotionState()) {
	  delete physobj->body->getMotionState();
	}
	delete physobj->body;
      }
      if(physobj->newshape != NULL) {
	delete physobj->shape;
	physobj->shape = physobj->newshape; physobj->newshape = NULL;
      }

      btScalar mass(50.f);
      if(physobj->objtype == OBJ_TYPE_PRIM) { mass = 0.0f; } // prims not physical yet
	
      btTransform transform;
      transform.setIdentity();
      if(physobj->objtype != OBJ_TYPE_AVATAR)
	transform.setRotation(physobj->rot.inverse());
      transform.setOrigin(physobj->pos);

      btDefaultMotionState* motion = new btDefaultMotionState(transform);
      btVector3 local_inertia(0,0,0);
      physobj->shape->calculateLocalInertia(mass, local_inertia);
      btRigidBody::btRigidBodyConstructionInfo body_info(mass, motion, 
							 physobj->shape,
							 local_inertia);
      physobj->body = new btRigidBody(body_info);
      if(physobj->objtype == OBJ_TYPE_AVATAR) {
	physobj->body->setAngularFactor(0.0f); /* Note: doesn't work on 2.73 */
	phys->dynamicsWorld->addRigidBody(physobj->body, COL_AVATAR, AVATAR_COLLIDES_WITH);
      } else if(physobj->objtype == OBJ_TYPE_PRIM) {
	physobj->body->setCollisionFlags( physobj->body->getCollisionFlags() |
					  btCollisionObject::CF_KINEMATIC_OBJECT);
	phys->dynamicsWorld->addRigidBody(physobj->body, COL_PRIM, PRIM_COLLIDES_WITH);
      }
    } else if(physobj->pos_update) {
      btTransform transform;
      transform.setIdentity();
      if(physobj->objtype != OBJ_TYPE_AVATAR)
	transform.setRotation(physobj->rot.inverse());
      transform.setOrigin(physobj->pos);
      // FIXME - handle rotation
      physobj->body->getMotionState()->setWorldTransform(transform);
      physobj->body->setActivationState(ACTIVE_TAG); // do we need this?
      physobj->pos_update = 0;
    }
  }

  phys->changed.clear();

}

// main physics thread
// TODO - we should suspend processing totally when nothing's happening
// (and also avoid unnecesary processing on stationary objects, if possible).
static gpointer physics_thread(gpointer data) {
  struct physics_ctx *phys = (struct physics_ctx*)data;
  GTimer *timer = g_timer_new();
  double next_step = 0.0;
  for(;;) {
    g_static_mutex_lock(&phys->mutex);

    do_phys_updates_locked(phys);

    for(std::set<phys_obj*>::iterator iter = phys->avatars.begin(); 
	iter != phys->avatars.end(); iter++) {
      struct phys_obj *physobj = *iter;
      btTransform trans;
      physobj->body->getMotionState()->getWorldTransform(trans);
      physobj->pos = trans.getOrigin();
      physobj->velocity = physobj->body->getLinearVelocity();
    }

    g_static_mutex_unlock(&phys->mutex);

    // poke the main thread. Note that this ain't really useful on the first pass
    write(phys->pipe[1], "", 1);

    // FIXME - this really doesn't look right...
    double time_skip = next_step - g_timer_elapsed(timer, NULL);
    next_step =  g_timer_elapsed(timer, NULL) + (1.0/60.0);
    if(time_skip > 0.0) {
      usleep(1000000*time_skip);
    }

    g_static_mutex_lock(&phys->mutex);
    if(phys->shutdown) {
      g_static_mutex_unlock(&phys->mutex); break;
    }
    do_phys_updates_locked(phys);
    for(std::set<phys_obj*>::iterator iter = phys->avatars.begin(); 
	iter != phys->avatars.end(); iter++) {
      struct phys_obj *physobj = *iter; 

      btVector3 impulse = physobj->target_velocity;
      assert(physobj->body != NULL);
      impulse -= physobj->body->getLinearVelocity();
      impulse *= 0.8f * 50.0f; // FIXME - don't hardcode mass

      if(!physobj->is_flying) impulse.setY(0.0f);
      physobj->body->applyCentralImpulse(impulse);

      if(physobj->target_velocity.getX() != 0.0f || 
	 physobj->target_velocity.getY() != 0.0f || 
	 physobj->target_velocity.getZ() != 0.0f) {
	physobj->body->setActivationState(ACTIVE_TAG);
      }
    }
    g_static_mutex_unlock(&phys->mutex);

    phys->dynamicsWorld->stepSimulation(1.f/60.f,10);
  }

  g_timer_destroy(timer); return NULL;
}

static gboolean got_poke(GIOChannel *source, GIOCondition cond, 
			 gpointer priv) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(cond & G_IO_IN) {
    { char buf[40]; read(phys->pipe[0], buf, 40); }
    
    g_static_mutex_lock(&phys->mutex);

    for(std::set<phys_obj*>::iterator iter = phys->avatars.begin(); 
	iter != phys->avatars.end(); iter++) {
      struct phys_obj *physobj = *iter;
      caj_vector3 newpos;

      if(physobj->obj == NULL) continue;

      newpos.x = physobj->pos.getX();
      newpos.y = physobj->pos.getZ(); // swap Y and Z
      newpos.z = physobj->pos.getY();
      
      physobj->obj->velocity.x = physobj->velocity.getX();
      physobj->obj->velocity.y = physobj->velocity.getZ();
      physobj->obj->velocity.z = physobj->velocity.getY();
    
      if(fabs(newpos.x - physobj->obj->pos.x) >= 0.01 ||
	 fabs(newpos.y - physobj->obj->pos.y) >= 0.01 ||
	 fabs(newpos.z - physobj->obj->pos.z) >= 0.01) {
	world_move_obj_int(phys->sim, physobj->obj, newpos);
      }
      
      // FIXME - TODO;
    }

   g_static_mutex_unlock(&phys->mutex);
  }
  if(cond & G_IO_NVAL) return FALSE; // blech;
  return TRUE;
}

static void destroy_physics(struct simulator_ctx *sim, void *priv) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;

  g_static_mutex_lock(&phys->mutex);
  phys->shutdown = 1;
  g_static_mutex_unlock(&phys->mutex);
  g_thread_join(phys->thread);

  // FIXME - TODO delete physics objects properly

  int count = phys->dynamicsWorld->getNumCollisionObjects();
  for(int i = count-1; i >= 0; i--) {
    btCollisionObject* obj = phys->dynamicsWorld->getCollisionObjectArray()[i];
    btRigidBody* body = btRigidBody::upcast(obj);
    if (body != NULL && body->getMotionState() != NULL) {
      delete body->getMotionState();
    }
    phys->dynamicsWorld->removeCollisionObject(obj);
    delete obj;
  }

  g_static_mutex_free(&phys->mutex);
 
  // delete various staticly-allocated shapes
  delete phys->plane_0x;
  delete phys->plane_1x;
  delete phys->plane_0y;
  delete phys->plane_1y;
  delete phys->ground_shape;

  delete phys->dynamicsWorld;
  delete phys->solver;
  delete phys->overlappingPairCache;
  delete phys->dispatcher;
  delete phys->collisionConfiguration;
  delete phys;
}

static btStaticPlaneShape* add_sim_boundary(struct physics_ctx *phys,
					    float x_dirn, float y_dirn, float off) {
  btVector3 norm(x_dirn, 0, y_dirn);
  btStaticPlaneShape* plane = new btStaticPlaneShape(norm,off);

  btTransform trans;
  trans.setIdentity();
  trans.setOrigin(btVector3(0,0,0));
  
  btDefaultMotionState* motion = new btDefaultMotionState(trans);
  btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f ,motion,plane,btVector3(0,0,0));
  btRigidBody* body = new btRigidBody(rbInfo);
  phys->dynamicsWorld->addRigidBody(body, COL_BORDER, BORDER_COLLIDES_WITH);
  return plane;
}

int cajeput_physics_init(int api_version, struct simulator_ctx *sim, 
			 void **priv, struct cajeput_physics_hooks *hooks) {
  struct physics_ctx *phys = new physics_ctx();
  *priv = phys; phys->sim = sim;

  hooks->add_object = add_object;
  hooks->del_object = del_object;
  hooks->upd_object_pos = upd_object_pos;
  hooks->upd_object_full = upd_object_full;
  hooks->set_force = set_force;
  hooks->set_target_velocity = set_target_velocity;
  hooks->set_avatar_flying = set_avatar_flying;
  hooks->destroy = destroy_physics;

  phys->collisionConfiguration = new btDefaultCollisionConfiguration();
  phys->dispatcher = new btCollisionDispatcher(phys->collisionConfiguration);
  btVector3 worldMin(0,0,0);
  btVector3 worldMax(256,WORLD_HEIGHT,256);

  // FIXME - variable name seems dodgy
  phys->overlappingPairCache = new bt32BitAxisSweep3(worldMin,worldMax,MAX_OBJECTS);

  phys->solver = new btSequentialImpulseConstraintSolver();
  phys->dynamicsWorld =  new btDiscreteDynamicsWorld(phys->dispatcher,
						     phys->overlappingPairCache,
						     phys->solver,
						     phys->collisionConfiguration);

  phys->dynamicsWorld->setGravity(btVector3(0,-9.8,0));

#if 0
  phys->ground_shape = new btBoxShape(btVector3(256,1,256));
  
  btTransform ground_transform;
  ground_transform.setIdentity();
  ground_transform.setOrigin(btVector3(128,24.5,128));
#else
  float *heightfield = sim_get_heightfield(sim);
  // WARNING: note that this does *NOT* make its own copy of the heightfield
  // FIXME - this limits max terrain height to 100 metres
  phys->ground_shape = new btHeightfieldTerrainShape(256, 256, heightfield,
						     0, 0.0f, 100.0f, 1,
						     PHY_FLOAT, 0 /* fixme - flip? */);
  btTransform ground_transform;
  ground_transform.setIdentity();
  ground_transform.setOrigin(btVector3(128,50,128));
#endif
  
  btDefaultMotionState* motion = new btDefaultMotionState(ground_transform);
  btRigidBody::btRigidBodyConstructionInfo body_info(0.0,motion,phys->ground_shape,btVector3(0,0,0));
  btRigidBody* body = new btRigidBody(body_info);
  phys->dynamicsWorld->addRigidBody(body, COL_GROUND, GROUND_COLLIDES_WITH);


  // Sim edges - will need to selectively remove when region
  // crossing is added
  phys->plane_0x = add_sim_boundary(phys,1.0f,0.0f,0.0f);
  phys->plane_1x = add_sim_boundary(phys,-1.0f,0.0f,-256.0f);
  phys->plane_0y = add_sim_boundary(phys,0.0f,1.0f,0.0f);
  phys->plane_1y = add_sim_boundary(phys,0.0f,-1.0f,-256.0f);
  // TODO - add ceiling

  phys->shutdown = 0;
  g_static_mutex_init(&phys->mutex);
  if(pipe(phys->pipe)) {
    printf("ERROR: couldn't create pipe for physics comms\n");
    exit(1);
  };
  phys->pipein = g_io_channel_unix_new(phys->pipe[0]);
  g_io_add_watch(phys->pipein, (GIOCondition)(G_IO_IN|G_IO_NVAL), got_poke, phys);

  phys->thread = g_thread_create(physics_thread, phys, TRUE, NULL);
  
  if(phys->thread == NULL) {
    printf("ERROR: couldn't create physics thread\n"); exit(1);
  }

  return TRUE;
}
