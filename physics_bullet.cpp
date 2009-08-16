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

#include "cajeput_core.h"
#include "btBulletDynamicsCommon.h"
#include "BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h"
#include <stdio.h> /* for debugging */
#include <set>

struct phys_obj;

struct physics_ctx {
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

  std::set<phys_obj*> avatars;
};

struct phys_obj {
  btCollisionShape* shape;
  btRigidBody* body;
  btVector3 target_velocity;
#if 0
  btPairCachingGhostObject* ghost;
  btKinematicCharacterController* control;
#endif
};

#define MAX_OBJECTS 15000

#define AVATAR_Z_FUDGE 0.2f

static void add_object(struct simulator_ctx *sim, void *priv,
		       struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->type == OBJ_TYPE_AVATAR) {
    struct phys_obj *physobj = new phys_obj();
    btScalar mass(50.f);
    obj->phys = physobj;
    physobj->shape = new btCapsuleShape(0.75f,0.25f); /* radius, height */
  
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(obj->pos.x,obj->pos.z+AVATAR_Z_FUDGE,obj->pos.y));
  
#if 1
    btDefaultMotionState* motion = new btDefaultMotionState(transform);
    btVector3 local_inertia(0,0,0);
    physobj->shape->calculateLocalInertia(mass, local_inertia);
    btRigidBody::btRigidBodyConstructionInfo body_info(mass, motion, 
						       physobj->shape,
						       local_inertia);
    physobj->body = new btRigidBody(body_info);
    physobj->body->setAngularFactor(0.0f); /* Note: doesn't work on 2.73 */
    phys->dynamicsWorld->addRigidBody(physobj->body);
#else
    physobj->ghost = new btPairCachingGhostObject();
    physobj->ghost->setWorldTransform(startTransform);
    // sweepBP->getOverlappingPairCache()->setInternalGhostPairCallback(new btGhostPairCallback());
    physobj->ghost->setCollisionShape(physobj->shape);
    physobj->ghost->setCollisionFlags (btCollisionObject::CF_CHARACTER_OBJECT);
    physobj->control = new btKinematicCharacterController(physobj->ghost, physobj->shape,
							  0.30 /* step height */);
    phys->dynamicsWorld->addCollisionObject(physobj->ghost);
    phys->dynamicsWorld->addAction(physobj->control);
#endif

    physobj->target_velocity = btVector3(0,0,0);
    phys->avatars.insert(physobj);
  } else {
    obj->phys = NULL;
  }
}

static void del_object(struct simulator_ctx *sim, void *priv,
		       struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->phys != NULL) {
#if 1
    struct phys_obj *physobj = (struct phys_obj *)obj->phys;
    phys->dynamicsWorld->removeCollisionObject(physobj->body);
    if(physobj->body->getMotionState()) {
      delete physobj->body->getMotionState();
    }
    delete physobj->body;
    delete physobj->shape;
#else
    phys->dynamicsWorld->removeCollisionObject(physobj->ghost);
    // ??? FIXME
#endif
    phys->avatars.erase(physobj);
    delete physobj;
    obj->phys = NULL;
  }
}

/* FIXME - the very existence of this is an evil hack */
static int update_pos(struct simulator_ctx *sim, void *priv,
		      struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->phys == NULL)
    return 0;

  {
    struct phys_obj *physobj = (struct phys_obj *)obj->phys;
    sl_vector3 newpos; btTransform trans;
    physobj->body->getMotionState()->getWorldTransform(trans);
    newpos.x = trans.getOrigin().getX();
    newpos.y = trans.getOrigin().getZ(); // swap Y and Z
    newpos.z = trans.getOrigin().getY() - AVATAR_Z_FUDGE;
    
    if(fabs(newpos.x - obj->pos.x) < 0.01 &&
       fabs(newpos.y - obj->pos.y) < 0.01 &&
       fabs(newpos.z - obj->pos.z) < 0.01)
      return 0;
    world_move_obj_int(sim, obj, newpos);
    
    return 1;
  }
}

/* FIXME - HACK */
static void set_force(struct simulator_ctx *sim, void *priv,
		      struct world_obj *obj, sl_vector3 force) {
  btVector3 real_force(force.x,force.z,force.y);
  if(obj->phys == NULL)
    return;
  struct phys_obj *physobj = (struct phys_obj *)obj->phys;
  //printf("DEBUG: applying force %f, %f, %f\n",force.x,force.y,force.z);
  physobj->body->clearForces(); /* FIXME - this resets rotation too! */
  physobj->body->applyCentralForce(real_force);
  if(force.x != 0.0f || force.y != 0.0f || force.z != 0.0f)
    physobj->body->setActivationState(ACTIVE_TAG);
}

static void set_target_velocity(struct simulator_ctx *sim, void *priv,
		      struct world_obj *obj, sl_vector3 velocity) {
  if(obj->phys == NULL)
    return;
  struct phys_obj *physobj = (struct phys_obj *)obj->phys;
  physobj->target_velocity = btVector3(velocity.x, velocity.z, velocity.y);
  if(velocity.x != 0.0f || velocity.y != 0.0f || velocity.z != 0.0f)
    physobj->body->setActivationState(ACTIVE_TAG);  
}

static void step_world(struct simulator_ctx *sim, void *priv) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;

  for(std::set<phys_obj*>::iterator iter = phys->avatars.begin(); 
      iter != phys->avatars.end(); iter++) {
    struct phys_obj *physobj = *iter;
    btVector3 impulse = physobj->target_velocity;
    impulse -= physobj->body->getLinearVelocity();
    impulse *= 0.8f * 50.0f; // FIXME - don't hardcode mass
    impulse.setY(0.0f);
    physobj->body->applyCentralImpulse(impulse);
  }

  phys->dynamicsWorld->stepSimulation(1.f/60.f,10);
}

static void destroy_physics(struct simulator_ctx *sim, void *priv) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;

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
  phys->dynamicsWorld->addRigidBody(body);
  return plane;
}

int cajeput_physics_init(int api_version, struct simulator_ctx *sim, 
			 void **priv, struct cajeput_physics_hooks *hooks) {
  struct physics_ctx *phys = new physics_ctx();
  *priv = phys;

  hooks->add_object = add_object;
  hooks->del_object = del_object;
  hooks->update_pos = update_pos;
  hooks->set_force = set_force;
  hooks->set_target_velocity = set_target_velocity;
  hooks->step = step_world;
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
  phys->dynamicsWorld->addRigidBody(body);


  // Sim edges - will need to selectively remove when region
  // crossing is added
  phys->plane_0x = add_sim_boundary(phys,1.0f,0.0f,0.0f);
  phys->plane_1x = add_sim_boundary(phys,-1.0f,0.0f,-256.0f);
  phys->plane_0y = add_sim_boundary(phys,0.0f,1.0f,0.0f);
  phys->plane_1y = add_sim_boundary(phys,0.0f,-1.0f,-256.0f);
  // TODO - add ceiling
}
