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

#include "cajeput_core.h" // for sim_get_heightfield
#include "cajeput_world.h"
#include "cajeput_prim.h"
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <stdio.h> /* for debugging */
#include <unistd.h>
#include <set>
#include <vector>
#include <deque>
#include <map>

#define GRAVITY 9.8

struct phys_obj;

typedef std::vector<caj_phys_collision> collisions_info;

struct physics_ctx {
  simulator_ctx *sim;

  GStaticMutex mutex;
  GThread *thread;

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
  std::set<phys_obj*> physical;
  std::set<phys_obj*> changed;
  int shutdown;
  std::deque<collisions_info*> collision_upds;
};

struct part_map {
  int num_parts;
  uint32_t parts[1];
};

struct phys_obj {
  // most of this is protected by the mutex
  btCollisionShape *shape, *newshape;
  part_map *parts, *newparts;
  btRigidBody* body;
  btVector3 target_velocity;
  btVector3 pos, velocity;
  btVector3 impulse;
  btVector4 footfall_tmp; // physics thread only
  caj_vector4 footfall;
  btQuaternion rot;
  int is_flying; // for avatars only
  int flying_changed;
  int is_deleted;
  int pos_update;
  int objtype;
  int phystype;
  world_obj *obj; // main thread use only
  int collide_down; // physics thread only; for avatars
  int collide_down_ticks; // for avatars, mutex protected. + colliding, - not.
  std::map<int,btTransform> child_pos_upd;
#if 0
  btPairCachingGhostObject* ghost;
  btKinematicCharacterController* control;
#endif
};

static gboolean phys_update_in_mt(gpointer priv);

// internal to this module
#define PHYS_TYPE_PHANTOM 0
#define PHYS_TYPE_NORMAL 1 /* collided with, but not physical */
#define PHYS_TYPE_PHYSICAL 2

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
#define PHYS_PRIM_COLLIDES_WITH (COL_GROUND|COL_PRIM|COL_PHYS_PRIM|COL_AVATAR)
#define GROUND_COLLIDES_WITH (COL_AVATAR|COL_PHYS_PRIM)
#define BORDER_COLLIDES_WITH COL_AVATAR

// Note: for llVolumeDetect, I suspect that
//  mBody->setCollisionFlags(mBody->getCollisionFlags() |
//                           btCollisionObject::CF_NO_CONTACT_RESPONSE));
// is going to be required

// we're going to have fun with this
// box:
//   PROFILE_SHAPE_SQUARE, PATH_CURVE_STRAIGHT
//   uses ProfileBegin/End, ("path cut" in UI
//        ProfileHollow, 
//        PathTwist, PathTwistBegin, ("twist" in UI, not 1:1 with UI controls)
//        PathScaleX/Y ("taper" in UI)
//        PathShearX/Y ("top shear" in UI)
//        PathBegin/End ("slice begin/end" in UI)
// prism: PROFILE_SHAPE_EQUIL_TRI, PATH_CURVE_STRAIGHT
//   uses same params as box
// cylinder:  PROFILE_SHAPE_CIRCLE, PATH_CURVE_STRAIGHT
//   again, same params as box
// sphere: 
//   PROFILE_SHAPE_SEMICIRC, PATH_CURVE_CIRCLE
//   uses PathBegin/End ("path cut" in UI)
//        ProfileHollow
//        PathTwist, PathTwistBegin, ("twist" in UI, not 1:1 with UI controls)
//        ProfileBegin/End, ("dimple" in UI)

struct coord_2d { float x, y; };

// handles PATH_CURVE_STRAIGHT with no twist, hollow or profile cut,
// i.e. convex shapes only. Concave shapes will need more complex handling.
static btConvexHullShape *make_boxlike_shape(primitive_obj *prim, 
					     coord_2d *profile, int num_points) {
  btConvexHullShape* hull = new btConvexHullShape();
  float x = prim->ob.scale.x/2.0f, y = prim->ob.scale.y/2.0f;
  float z = prim->ob.scale.z/2.0f, z_top = z, z_bottom = -z;
  float x_top = x, y_top = y, x_bottom = x, y_bottom = y;

  if(prim->path_scale_x <= 100) x_bottom *= prim->path_scale_x/100.0f;
  else if(prim->path_scale_x <= 200) x_top *= (200-prim->path_scale_x)/100.0f;

  if(prim->path_scale_y <= 100) y_bottom *= prim->path_scale_y/100.0f;
  else if(prim->path_scale_y <= 200) y_top *= (200-prim->path_scale_y)/100.0f;

  float x_shear = (prim->path_shear_x > 128 ? prim->path_shear_x-256 : prim->path_shear_x)/50.0f*x;
  float y_shear = (prim->path_shear_y > 128 ? prim->path_shear_y-256 : prim->path_shear_y)/50.0f*y;

  if(prim->path_begin != 0 || prim->path_end != 0) {
    float newtop, newbottom;
    z_bottom = z_bottom + (2.0f*z)*(prim->path_begin/50000.0f);
    z_top = z_top - (2.0f*z)*(prim->path_end/50000.0f);

    newtop = x_top + (x_bottom-x_top)*(prim->path_end/50000.0f);
    newbottom = x_bottom + (x_top-x_bottom)*(prim->path_begin/50000.0f);
    x_top = newtop; x_bottom = newbottom;
    
    newtop = y_top + (y_bottom-y_top)*(prim->path_end/50000.0f);
    newbottom = y_bottom + (y_top-y_bottom)*(prim->path_begin/50000.0f);
    y_top = newtop; y_bottom = newbottom;
  }

  printf("DEBUG: bottom %fx%f @ %f, top %fx%f @ %f\n",
	 x_bottom, y_bottom, z_bottom, x_top, y_top, z_top);

  for(int i = 0; i < num_points; i++) {
    // it'd be nice if we removed duplicate points, but that's a pain
    hull->addPoint(btVector3(profile[i].x * x_bottom, z_bottom, 
			     profile[i].y * y_bottom));
    hull->addPoint(btVector3(x_shear + profile[i].x * x_top, z_top,
			     y_shear + profile[i].y * y_top));
  }
  return hull;
}

static coord_2d square_profile[] = { { -1.0f, -1.0f },
				     { 1.0f, -1.0f },
				     { -1.0f, 1.0f },
				     { 1.0f, 1.0f }};
static coord_2d equil_tri_profile[] = { { 1.0f, 0.0f} , 
					{ -0.7320508075688772f, -1.0f },
					{ -0.7320508075688772f, 1.0f } };
static coord_2d circle_profile_8[] = { { 0.000000f, 1.000000f },
				       { 0.707107f, 0.707107f },
				       { 1.000000f, 0.000000f },
				       { 0.707107f, -0.707107f },
				       { 0.000000f, -1.000000f },
				       { -0.707107f, -0.707107f },
				       { -1.000000f, -0.000000f },
				       { -0.707107f, 0.707107f } };
				       

static btCollisionShape* shape_from_obj_part(struct world_obj *obj) {
  if(obj->type == OBJ_TYPE_AVATAR) {
    // FIXME - take into account avatar's height
    return new btCapsuleShape(0.25f,1.25f); /* radius, height */
  } else if (obj->type == OBJ_TYPE_PRIM) {
    primitive_obj *prim = (primitive_obj*)obj;
    if((prim->path_curve & PATH_CURVE_MASK) == PATH_CURVE_STRAIGHT &&
       prim->profile_hollow == 0 && 
       prim->profile_begin == 0 && prim->profile_end == 0 &&
       prim->path_twist == 0 && prim->path_twist_begin == 0) {
      // so long as we have no twist, hollow, or profile cut this is a convex object
      
      if((prim->profile_curve & PROFILE_SHAPE_MASK) == PROFILE_SHAPE_SQUARE &&
	 prim->path_scale_x == 100 && prim->path_scale_y == 100 &&
	 prim->path_shear_x == 0 && prim->path_shear_y == 0 &&
	 prim->path_begin == 0 && prim->path_end == 0) {
	// in theory, we can handle non-zero PathBegin/End here, but we'd need an offset
	//printf("DEBUG: got a box prim in physics\n");
	return new btBoxShape(btVector3(obj->scale.x/2.0f, obj->scale.z/2.0f, obj->scale.y/2.0f));
      } else if((prim->profile_curve & PROFILE_SHAPE_MASK) == PROFILE_SHAPE_CIRCLE &&
		prim->path_scale_x == 100 && prim->path_scale_y == 100 &&
		prim->path_shear_x == 0 && prim->path_shear_y == 0 &&
		prim->path_begin == 0 && prim->path_end == 0 && 
		prim->ob.scale.x == prim->ob.scale.z) {
	// has to be a perfect cylinder for this to work
	//printf("DEBUG: got a cylinder prim in physics\n");
	return new btCylinderShape(btVector3(obj->scale.x/2.0f, obj->scale.z/2.0f, obj->scale.y/2.0f));
      } else {
	//printf("DEBUG: got a convex boxlike prim in physics\n");
	switch(prim->profile_curve & PROFILE_SHAPE_MASK) {
	case PROFILE_SHAPE_SQUARE:
	  return make_boxlike_shape(prim, square_profile, 4);
	case PROFILE_SHAPE_EQUIL_TRI:
	  // you may think we have to take into account that the centre of the
	  // triangle isn't the center of the object when tapering. You'd be
	  // wrong - the viewer doesn't. Try it for yourself - create a big 
	  // prism, taper it to a point, and stick an 0.1m sphere on the tip!
	  return make_boxlike_shape(prim, equil_tri_profile, 3);
	case PROFILE_SHAPE_CIRCLE:
	  // for now, we model a circle as an octagon.
	  // FIXME: probably need higher LOD for larger objects
	  return make_boxlike_shape(prim, circle_profile_8, 8);
	default:
	  // I don't think any other shapes are used by the viewer, but they're
	  // theoretically possible.
	  printf("WARNING: unhandled profile shape in make_boxlike_shape path\n");
	  // this is a closer approximation than a plain bounding box
	  return make_boxlike_shape(prim, square_profile, 4);
	}
      }

    } else if((prim->path_curve & PATH_CURVE_MASK) == PATH_CURVE_CIRCLE && 
	      (prim->profile_curve & PROFILE_SHAPE_MASK) == PROFILE_SHAPE_SEMICIRC &&
	      prim->path_begin == 0 && prim->path_end == 0 &&
	      prim->profile_hollow == 0 && 
	      prim->profile_begin == 0 && prim->profile_end == 0 &&
	      prim->path_twist == 0 && prim->path_twist_begin == 0) {
      if(prim->ob.scale.x == prim->ob.scale.y && 
	 prim->ob.scale.x == prim->ob.scale.z) {
	//printf("DEBUG: got a sphere prim in physics\n");
	return new btSphereShape(obj->scale.x/2.0f);
      } else {
	// FIXME - implement this. How, though?
	// Suspect I'll end up approximating it using btConvexHullShape
	printf("FIXME: got a spheroidal prim in physics\n");
	return new btBoxShape(btVector3(obj->scale.x/2.0f, obj->scale.z/2.0f, obj->scale.y/2.0f));
      }
    } else {
      printf("WARNING: unhandled prim shape in physics\n");
      return new btBoxShape(btVector3(obj->scale.x/2.0f, obj->scale.z/2.0f, obj->scale.y/2.0f));
    }
  } else { 
    assert(0); return NULL;
  }
}

static btCollisionShape* shape_from_obj(struct world_obj *obj) {
  btCollisionShape *root_shape = shape_from_obj_part(obj);
  if(obj->type != OBJ_TYPE_PRIM) return root_shape;
  
  primitive_obj *prim = (primitive_obj*)obj;
  if(prim->num_children == 0) return root_shape;

  btCompoundShape *compound = new btCompoundShape();
  btTransform trans;

  trans.setIdentity();
  compound->addChildShape(trans, root_shape);
  for(int i = 0; i < prim->num_children; i++) {
    primitive_obj *child = prim->children[i];
    btQuaternion rot(child->ob.rot.x,child->ob.rot.z,
		     child->ob.rot.y,child->ob.rot.w);
    // don't think we need to clear the translation
    trans.setOrigin(btVector3(child->ob.local_pos.x,
			      child->ob.local_pos.z,
			      child->ob.local_pos.y));
    trans.setRotation(rot.inverse());
    compound->addChildShape(trans, shape_from_obj_part(&child->ob));
  }
  return compound;
}

static part_map* make_part_map(struct world_obj *obj) {
  if(obj->type == OBJ_TYPE_PRIM) {
    primitive_obj *prim = (primitive_obj*)obj;
    part_map *parts = (part_map*)malloc(offsetof(part_map, parts)+
					sizeof(uint32_t)*(prim->num_children+1));
    parts->num_parts = prim->num_children+1;
    parts->parts[0] = prim->ob.local_id;
    for(int i = 0; i < prim->num_children; i++) {
      parts->parts[i+1] = prim->children[i]->ob.local_id;
    }
    return parts;
  } else if(obj->type == OBJ_TYPE_AVATAR) {
    part_map *parts = (part_map*)malloc(offsetof(part_map, parts)+
					sizeof(uint32_t));
    parts->num_parts = 1; parts->parts[0] = obj->local_id;
    return parts;
  } else {
    return NULL;
  }
}

static int compute_phys_type(struct world_obj *obj) {
  if(obj->type == OBJ_TYPE_AVATAR) {
    return PHYS_TYPE_PHYSICAL;
  } else if(obj->type == OBJ_TYPE_PRIM) {
    primitive_obj *prim = (primitive_obj*)obj;
    if(prim->flags & PRIM_FLAG_PHANTOM || prim->ob.parent != NULL) {
      // FIXME - can't handle child prims, so disable physics on them for now
      return PHYS_TYPE_PHANTOM;
    } else if(prim->flags & PRIM_FLAG_PHYSICAL) {
      return PHYS_TYPE_PHYSICAL;
    } else {
      return PHYS_TYPE_NORMAL;
    }
  } else {
    return PHYS_TYPE_PHANTOM;
  }
}

static void add_object(struct simulator_ctx *sim, void *priv,
		       struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  int phys_type = compute_phys_type(obj);
  if(phys_type != PHYS_TYPE_PHANTOM) {
    struct phys_obj *physobj = new phys_obj();
    obj->phys = physobj; 

    physobj->phystype = phys_type;
    physobj->shape = shape_from_obj(obj); physobj->newshape = NULL;
    physobj->parts = make_part_map(obj); physobj->newparts = NULL;
    physobj->body = NULL; 
    physobj->pos = btVector3(obj->local_pos.x,obj->local_pos.z,obj->local_pos.y);
    physobj->footfall_tmp = btVector4(0,0,0,1.0f);
    if(obj->type == OBJ_TYPE_AVATAR) {
      physobj->rot = btQuaternion(0.0,0.0,0.0,1.0);
      physobj->is_flying = 1; // probably the safe assumption. FIXME - do right.
      physobj->collide_down = 0; physobj->collide_down_ticks = 0; 
    } else {
       physobj->rot = btQuaternion(obj->rot.x,obj->rot.z,obj->rot.y,obj->rot.w);
    }
    physobj->target_velocity = btVector3(0,0,0);
    physobj->velocity = btVector3(0,0,0); // FIXME - load from prim?
    physobj->impulse = btVector3(0,0,0);
    
    physobj->is_deleted = 0; physobj->pos_update = 0; 
    physobj->flying_changed = 0;
    physobj->obj = obj;
    physobj->objtype = obj->type; // can't safely access obj in thread


    g_static_mutex_lock(&phys->mutex);
    if(phys_type == PHYS_TYPE_PHYSICAL)
      phys->physical.insert(physobj);
    phys->changed.insert(physobj);
    g_static_mutex_unlock(&phys->mutex);
  } else {
    obj->phys = NULL;
  }
}

static void upd_child_pos(struct simulator_ctx *sim, physics_ctx *phys,
			  primitive_obj *parent, world_obj *child) {
  if(parent->ob.phys == NULL) return;
  phys_obj *physobj = (phys_obj*)parent->ob.phys;

  for(int i = 0; i < parent->num_children; i++) {
    if(parent->children[i] == (primitive_obj*)child) {
      btTransform trans;
      trans.setIdentity();
      trans.setOrigin(btVector3(child->local_pos.x,child->local_pos.z,
				child->local_pos.y));
      trans.setRotation(btQuaternion(child->rot.x,child->rot.z,child->rot.y,
				     child->rot.w).inverse());
      g_static_mutex_lock(&phys->mutex);
      physobj->child_pos_upd[i+1] = trans;
      phys->changed.insert(physobj);
      g_static_mutex_unlock(&phys->mutex);
      return;
    }
  }
  
}

static void upd_object_pos(struct simulator_ctx *sim, void *priv,
			   struct world_obj *obj) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  if(obj->phys != NULL) {
    struct phys_obj *physobj = (struct phys_obj *)obj->phys;
    g_static_mutex_lock(&phys->mutex);
    physobj->pos = btVector3(obj->local_pos.x,obj->local_pos.z,obj->local_pos.y); 
    physobj->rot = btQuaternion(obj->rot.x,obj->rot.z,obj->rot.y,obj->rot.w);
    printf("DEBUG: object rotation <%f,%f,%f,%f>\n",obj->rot.x,obj->rot.y,obj->rot.z,obj->rot.w);
    physobj->pos_update = 1;
    phys->changed.insert(physobj);
    g_static_mutex_unlock(&phys->mutex);
  } else if(obj->parent != NULL && obj->parent->type == OBJ_TYPE_PRIM) {
    upd_child_pos(sim, phys, (primitive_obj*)obj->parent, obj);
  }
}

static void free_shape(btCollisionShape *shape) {
  // FIXME - we really don't want to use dynamic_cast if possible
  btCompoundShape *compound = dynamic_cast<btCompoundShape*>(shape);
  if(compound != NULL) {
    int count = compound->getNumChildShapes();
    for(int i = 0; i < count; i++) {
      delete compound->getChildShape(i);
    }
  }
  delete shape;
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
      free_shape(physobj->newshape); physobj->newshape = NULL;
      free(physobj->newparts);
    }
    g_static_mutex_unlock(&phys->mutex);
    obj->phys = NULL;
  }
}

static void upd_object_full(struct simulator_ctx *sim, physics_ctx *phys,
			    struct world_obj *obj, int phys_type) {
  if(obj->type != OBJ_TYPE_PRIM && obj->type != OBJ_TYPE_AVATAR) {
    upd_object_pos(sim, phys, obj);
  } else {
    if(obj->parent != NULL) {
      del_object(sim, phys, obj);
      if(obj->parent->type == OBJ_TYPE_PRIM)
	upd_object_full(sim, phys, obj->parent, compute_phys_type(obj->parent));
    } else if(phys_type == PHYS_TYPE_PHANTOM) {
      del_object(sim, phys, obj);
    } else if(obj->phys == NULL) {
      add_object(sim, phys, obj);
    } else {
      // FIXME - shouldn't *really* need to regenerate the shape if we're just
      // making an object physical (though currently we do have to, or the 
      // physics thread won't pick up the update).
      struct phys_obj *physobj = (struct phys_obj *)obj->phys;

      g_static_mutex_lock(&phys->mutex);

      free_shape(physobj->newshape);
      physobj->newshape = shape_from_obj(obj);
      physobj->newparts = make_part_map(obj);

      physobj->pos = btVector3(obj->local_pos.x,obj->local_pos.z,obj->local_pos.y);  // don't always want this, but...
      physobj->rot = btQuaternion(obj->rot.x,obj->rot.z,obj->rot.y,obj->rot.w);
      physobj->child_pos_upd.clear();
      physobj->pos_update = 0; physobj->phystype = phys_type;
      phys->changed.insert(physobj);

      // FIXME - should really only do this if phys_type has changed...
      if(phys_type == PHYS_TYPE_PHYSICAL)
	phys->physical.insert(physobj);
      else phys->physical.erase(physobj);

      g_static_mutex_unlock(&phys->mutex);
    }
  }
}

static void upd_object(struct simulator_ctx *sim, void *priv,
		       struct world_obj *obj, int update_flags) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;

  if(update_flags & CAJ_OBJUPD_CREATED)
      assert(obj->phys == NULL);

  int new_phys_type = compute_phys_type(obj);
  int phys_type = obj->phys == NULL ? PHYS_TYPE_PHANTOM : 
    ((phys_obj*) obj->phys)->phystype;

  if((update_flags & (CAJ_OBJUPD_PARENT|CAJ_OBJUPD_CREATED)) && obj->parent != NULL) {
    // we update the parent object when we get the CAJ_OBJUPD_PARENT for it
    del_object(sim, phys, obj);
  } else if((update_flags & (CAJ_OBJUPD_SHAPE|CAJ_OBJUPD_SCALE|
			     CAJ_OBJUPD_CREATED|CAJ_OBJUPD_CHILDREN|
			     CAJ_OBJUPD_PARENT)) || 
     new_phys_type != phys_type) {
    upd_object_full(sim, phys, obj, new_phys_type);
  } else if(update_flags & CAJ_OBJUPD_POSROT) {
    upd_object_pos(sim, phys, obj);
  }
}

#if 0 // this is outdated and needs rewriting if it's ever used
static void set_force(struct simulator_ctx *sim, void *priv,
		      struct world_obj *obj, caj_vector3 force) {
  btVector3 real_force(force.x,force.z,force.y);
  if(obj->phys == NULL)
    return;
  struct phys_obj *physobj = (struct phys_obj *)obj->phys;
  //printf("DEBUG: applying force %f, %f, %f\n",force.x,force.y,force.z);
  physobj->body->clearForces(); /* this resets rotation too! Not wanted! */
  physobj->body->applyCentralForce(real_force);
  if(force.x != 0.0f || force.y != 0.0f || force.z != 0.0f)
    physobj->body->setActivationState(ACTIVE_TAG);
}
#endif

static void apply_impulse(struct simulator_ctx *sim, void *priv,
			  struct world_obj *obj, caj_vector3 impulse,
			  int is_local) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
  struct phys_obj *physobj = (struct phys_obj *)obj->phys;
  
  // FIXME - handle impulses in local reference frame
  g_static_mutex_lock(&phys->mutex);
  physobj->impulse += btVector3(impulse.x, impulse.z, impulse.y);
  phys->changed.insert(physobj);
  g_static_mutex_unlock(&phys->mutex);
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


  if(physobj->is_flying != is_flying) {
    printf("DEBUG: is_flying changed to %i\n", is_flying);
    g_static_mutex_lock(&phys->mutex); // is_flying only changed from main thread
    physobj->is_flying = is_flying;
    physobj->flying_changed = 1;
    phys->changed.insert(physobj);
    g_static_mutex_unlock(&phys->mutex);    
  }
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
      free_shape(physobj->shape); free(physobj->parts);
      delete physobj;

      phys->physical.erase(physobj); // should really only do this for physical objs

      continue; // we don't want to do any further processing for this
    } 
    if(physobj->body == NULL || physobj->newshape != NULL) {
      assert(physobj->shape != NULL);
      if(physobj->body != NULL) {
	phys->dynamicsWorld->removeCollisionObject(physobj->body);
	if(physobj->body->getMotionState()) {
	  delete physobj->body->getMotionState();
	}
	delete physobj->body;
      }
      if(physobj->newshape != NULL) {
	free_shape(physobj->shape); free(physobj->parts);
	physobj->shape = physobj->newshape; physobj->newshape = NULL;
	physobj->parts = physobj->newparts; physobj->newparts = NULL;
      }

      btScalar mass;
      if(physobj->phystype != PHYS_TYPE_PHYSICAL) 
	mass = 0.0f;
      else if(physobj->objtype == OBJ_TYPE_AVATAR)
	mass = 50.0f;
      else mass = 10.0f; // FIXME - dynamic mass?
	
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
      physobj->body->setUserPointer(physobj);
      // numbers picked at random. FIXME? SL actually has 0 linear damping.
      physobj->body->setDamping(0.1f, 0.2f); 
      if(physobj->objtype == OBJ_TYPE_AVATAR) {
	printf("DEBUG: adding avatar\n");
	physobj->body->setAngularFactor(0.0f); /* Note: doesn't work on 2.73 */
	phys->dynamicsWorld->addRigidBody(physobj->body, COL_AVATAR, AVATAR_COLLIDES_WITH);

	if(physobj->is_flying)
	  physobj->body->setGravity(btVector3(0,0,0));
	else
	  physobj->body->setGravity(btVector3(0,-GRAVITY,0));
	physobj->flying_changed = 0;
      } else if(physobj->phystype == PHYS_TYPE_PHYSICAL) {
	printf("DEBUG: adding physical prim, mass %f\n", (double)mass);
	phys->dynamicsWorld->addRigidBody(physobj->body, COL_PHYS_PRIM, PHYS_PRIM_COLLIDES_WITH);
      } else {
	printf("DEBUG: adding normal prim\n");
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
      physobj->body->getMotionState()->setWorldTransform(transform);
      physobj->body->setWorldTransform(transform); // needed for physical objects
      physobj->body->activate(TRUE); // very much necessary
      physobj->pos_update = 0;
    }

    if(!physobj->child_pos_upd.empty()) {
      // FIXME - use static_cast once this is fully debugged
      btCompoundShape *shape = dynamic_cast<btCompoundShape*>(physobj->shape);
      assert(shape != NULL);
      for(std::map<int,btTransform>::iterator iter = physobj->child_pos_upd.begin();
	  iter != physobj->child_pos_upd.end(); iter++) {
	assert(iter->first < shape->getNumChildShapes());
	shape->updateChildTransform(iter->first, iter->second);
      }

      // FIXME - code duplication!
      btScalar mass;
      if(physobj->phystype != PHYS_TYPE_PHYSICAL) 
	mass = 0.0f;
      else if(physobj->objtype == OBJ_TYPE_AVATAR)
	mass = 50.0f;
      else mass = 10.0f; // FIXME - dynamic mass?
  
      btVector3 inertia;
      shape->calculateLocalInertia(mass,inertia);
      physobj->body->setMassProps(mass,inertia);
      physobj->body->updateInertiaTensor();

      // not 100% sure why this is needed, but otherwise collisions don't
      // work right afterwards.
      physobj->body->activate(TRUE);

      physobj->child_pos_upd.clear();
    }

    if(physobj->flying_changed) {
      assert(physobj->objtype == OBJ_TYPE_AVATAR);
      if(physobj->is_flying) {
	printf("DEBUG: setting avatar buoyant\n");
	physobj->body->setGravity(btVector3(0,0,0));
      } else {
	printf("DEBUG: setting avatar non-buoyant\n");
	physobj->body->setGravity(btVector3(0,-GRAVITY,0));
	physobj->body->activate();
      }
      physobj->flying_changed = 0;
      
    }

    if(physobj->impulse.getX() != 0.0 || physobj->impulse.getY() != 0.0 ||
       physobj->impulse.getZ() != 0.0) {
      physobj->body->applyCentralImpulse(physobj->impulse); 
      physobj->body->activate();
      physobj->impulse = btVector3(0.0, 0.0, 0.0);
    }
  }

  phys->changed.clear();

}

#define COLLIDE_DOWN_ANGLE (M_PI*(5.0/6.0)) /* 30 degrees limit */

uint32_t get_collider_id(phys_obj *physobj, int part_id) {
  if(physobj->parts->num_parts > 1) {
    assert(part_id >= 0 && part_id < physobj->parts->num_parts);
    return physobj->parts->parts[part_id];
  } else {
    return physobj->parts->parts[0];
  }
}

// technically, the footfall stuff isn't quite right - we want the 
// surface normal of the prim, not the collision normal. Works, though.
// FIXME - bunch of code duplication here that should be cleaned up
void tick_callback(btDynamicsWorld *world, btScalar timeStep) {
  struct physics_ctx *phys = (struct physics_ctx*)world->getWorldUserInfo();

  g_static_mutex_lock(&phys->mutex); // protects phys->physical
  for(std::set<phys_obj*>::iterator iter = phys->physical.begin(); 
      iter != phys->physical.end(); iter++) {
    struct phys_obj *physobj = *iter;
    physobj->collide_down = 0;
    // yes, this is right, even though footfall's not a quaternion!
    physobj->footfall_tmp = btVector4(0,0,0,1.0f);
  }
  g_static_mutex_unlock(&phys->mutex);

  collisions_info *collisions = new collisions_info();

  int numManifolds = world->getDispatcher()->getNumManifolds();
  for (int i=0; i<numManifolds; i++) {
    btPersistentManifold* contactManifold = world->getDispatcher()->getManifoldByIndexInternal(i);
    int numContacts = contactManifold->getNumContacts();
    
    btCollisionObject* obA = static_cast<btCollisionObject*>(contactManifold->getBody0());
    btCollisionObject* obB = static_cast<btCollisionObject*>(contactManifold->getBody1());
    phys_obj *physobjA = (phys_obj*)obA->getUserPointer();
    phys_obj *physobjB = (phys_obj*)obB->getUserPointer();
    
     if(physobjA != NULL && physobjA->objtype == OBJ_TYPE_AVATAR &&
	(physobjB == NULL || physobjB->objtype != OBJ_TYPE_AVATAR)) {
       for (int j=0; j<numContacts; j++) {
	 btManifoldPoint& pt = contactManifold->getContactPoint(j);
	 if (pt.getDistance() < 0.0f) {
	   //const btVector3& ptA = pt.getPositionWorldOnA();
	   const btVector3& ptB = pt.getPositionWorldOnB();
	   const btVector3& normal = -pt.m_normalWorldOnB;
	   btScalar angle = normal.angle(btVector3(0.0f,1.0f,0.0f));
	   
	   /* printf("DEBUG: collision, avatar A, normal <%f,%f,%f> angle %f\n",
	      normalOnB.getX(), normalOnB.getY(), normalOnB.getZ(), angle); */
	   if(angle > COLLIDE_DOWN_ANGLE) {
	     physobjA->collide_down = 1;
	     if(obB->getCollisionShape() != phys->ground_shape)
	       physobjA->footfall_tmp = btVector4(-normal.getX(), -normal.getY(),
						  -normal.getZ(),
						  -normal.dot(ptB));
	   }
	 }
       }
     } else if(physobjB != NULL && physobjB->objtype == OBJ_TYPE_AVATAR) {
       for (int j=0; j<numContacts; j++) {
	 btManifoldPoint& pt = contactManifold->getContactPoint(j);
	 if (pt.getDistance() < 0.0f) {
	   const btVector3& ptA = pt.getPositionWorldOnA();
	   //const btVector3& ptB = pt.getPositionWorldOnB();
	   const btVector3& normal = pt.m_normalWorldOnB;
	   btScalar angle = normal.angle(btVector3(0.0f,1.0f,0.0f));
	   /* printf("DEBUG: collision, avatar B, normal <%f,%f,%f>, angle %f\n",
	      normalOnB.getX(), normalOnB.getY(), normalOnB.getZ(), angle); */
	   if(angle > COLLIDE_DOWN_ANGLE) {
	     physobjB->collide_down = 1;
	     if(obB->getCollisionShape() != phys->ground_shape)
	       physobjB->footfall_tmp = btVector4(-normal.getX(), -normal.getY(),
						  -normal.getZ(),
						  -normal.dot(ptA));
	   }
	 }
       }
     }

     // FIXME - tidy this code up.
     if(physobjA != NULL && physobjA->objtype == OBJ_TYPE_PRIM &&
	physobjA->parts != NULL && physobjB != NULL && physobjB->parts != NULL) {
       for (int j=0; j<numContacts; j++) {
	 btManifoldPoint& pt = contactManifold->getContactPoint(j);
	 if (pt.getDistance() < 0.005f) {
	   caj_phys_collision collision;
	   collision.collidee = get_collider_id(physobjA, pt.m_index0);
	   collision.collider = physobjB->parts->parts[0];
	   collisions->push_back(collision);
	 }
       }
     }

     if(physobjB != NULL && physobjB->objtype == OBJ_TYPE_PRIM &&
	physobjB->parts != NULL && physobjA != NULL && physobjA->parts != NULL) {
       for (int j=0; j<numContacts; j++) {
	 btManifoldPoint& pt = contactManifold->getContactPoint(j);
	 if (pt.getDistance() < 0.005f) {
	   caj_phys_collision collision;
	   collision.collidee = get_collider_id(physobjB, pt.m_index1);
	   collision.collider = physobjA->parts->parts[0];
	   collisions->push_back(collision);
	 }
       }
     }
   }

  g_static_mutex_lock(&phys->mutex);
  for(std::set<phys_obj*>::iterator iter = phys->physical.begin(); 
      iter != phys->physical.end(); iter++) {
    struct phys_obj *physobj = *iter;
    if(physobj->objtype != OBJ_TYPE_AVATAR) continue;
    if(physobj->collide_down) {
      if(physobj->collide_down_ticks >= 0) physobj->collide_down_ticks++;
      else physobj->collide_down_ticks = 1;
    } else {
      if(physobj->collide_down_ticks <= 0) physobj->collide_down_ticks--;
      else physobj->collide_down_ticks = -1;
    }
  }
  phys->collision_upds.push_back(collisions);
  g_static_mutex_unlock(&phys->mutex);

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

    for(std::set<phys_obj*>::iterator iter = phys->physical.begin(); 
	iter != phys->physical.end(); iter++) {
      struct phys_obj *physobj = *iter;
      btTransform trans;
      physobj->body->getMotionState()->getWorldTransform(trans);
      physobj->pos = trans.getOrigin();
      physobj->rot = trans.getRotation().inverse();
      physobj->velocity = physobj->body->getLinearVelocity();
      if(physobj->objtype == OBJ_TYPE_AVATAR) {
	physobj->footfall.x = physobj->footfall_tmp.getX();
	physobj->footfall.y = physobj->footfall_tmp.getZ();
	physobj->footfall.z = physobj->footfall_tmp.getY();
	physobj->footfall.w = physobj->footfall_tmp.getW();
      }
    }

    g_static_mutex_unlock(&phys->mutex);

    // poke the main thread. Note that this ain't really useful on the first pass
    if(g_idle_add(phys_update_in_mt, phys) == 0) {
      printf("WARNING: couldn't poke main thread in physics code\n");
    }

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
    for(std::set<phys_obj*>::iterator iter = phys->physical.begin(); 
	iter != phys->physical.end(); iter++) {
      struct phys_obj *physobj = *iter; 

      // FIXME - generalise this to more general target velocity support?
      if(physobj->objtype != OBJ_TYPE_AVATAR) continue;

      btVector3 impulse = physobj->target_velocity;
      assert(physobj->body != NULL);
      impulse -= physobj->body->getLinearVelocity();
      impulse *= 0.9f * 50.0f; // FIXME - don't hardcode mass

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

// the part of the code handling physics updates that runs in the main thread
static gboolean phys_update_in_mt(gpointer priv) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;
    
  g_static_mutex_lock(&phys->mutex);

  for(std::set<phys_obj*>::iterator iter = phys->physical.begin(); 
      iter != phys->physical.end(); iter++) {
    struct phys_obj *physobj = *iter;
    caj_vector3 newpos; caj_quat newrot;

    if(physobj->obj == NULL) continue;

    newpos.x = physobj->pos.getX();
    newpos.y = physobj->pos.getZ(); // swap Y and Z
    newpos.z = physobj->pos.getY();

    newrot.x = physobj->rot.getX();
    newrot.y = physobj->rot.getZ();
    newrot.z = physobj->rot.getY();
    newrot.w = physobj->rot.getW();
      
    physobj->obj->velocity.x = physobj->velocity.getX();
    physobj->obj->velocity.y = physobj->velocity.getZ();
    physobj->obj->velocity.z = physobj->velocity.getY();

    if(physobj->objtype == OBJ_TYPE_AVATAR)
      avatar_set_footfall(phys->sim, physobj->obj,
			  &physobj->footfall);

    assert(physobj->obj->parent == NULL);
    
    if(fabs(newpos.x - physobj->obj->local_pos.x) >= 0.01 ||
       fabs(newpos.y - physobj->obj->local_pos.y) >= 0.01 ||
       fabs(newpos.z - physobj->obj->local_pos.z) >= 0.01 ||
       fabs(newrot.x - physobj->obj->rot.x) >= 0.01 ||
       fabs(newrot.y - physobj->obj->rot.y) >= 0.01 ||
       fabs(newrot.z - physobj->obj->rot.z) >= 0.01 ||
       fabs(newrot.w - physobj->obj->rot.w) >= 0.01) {
      // FIXME - probably should send an update if velocity (previous or 
      // current) is non-zero, too.
      if(physobj->objtype != OBJ_TYPE_AVATAR)
	physobj->obj->rot = newrot; // FIXME - may have to change once linking added
      world_move_obj_from_phys(phys->sim, physobj->obj, &newpos);
    }
  }

  int coll_upd_cnt = phys->collision_upds.size();
  while(coll_upd_cnt-- > 0 && !phys->collision_upds.empty()) {
    collisions_info *collisions = phys->collision_upds.front();
    phys->collision_upds.pop_front();
    g_static_mutex_unlock(&phys->mutex);
    // the C++ STL standard apparently does now guarantee the array elements
    // are stored contiguously in memory, so this should be safe...
    world_update_collisions(phys->sim, &(*collisions->begin()), collisions->size());
    g_static_mutex_lock(&phys->mutex);
    delete collisions;
  }

  g_static_mutex_unlock(&phys->mutex);

  return FALSE; // clear this idle callback
}

static void destroy_physics(struct simulator_ctx *sim, void *priv) {
  struct physics_ctx *phys = (struct physics_ctx*)priv;

  g_static_mutex_lock(&phys->mutex);
  phys->shutdown = 1;
  g_static_mutex_unlock(&phys->mutex);
  g_thread_join(phys->thread);

  // cancel any remaining pending updates.
  while(g_idle_remove_by_data(phys)) { }

  // You know I said this is only called from the physics thread? That's not
  // quite true. After we've shut down physics, there may be deleted objects
  // that need cleaning up, and this is as good a place as any to do so.
  do_phys_updates_locked(phys);

  // is this needed anymore? Yes, for stuff like the ground.
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

   while(!phys->collision_upds.empty()) {
      collisions_info *collisions = phys->collision_upds.front();
      phys->collision_upds.pop_front();
      delete collisions;
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

  hooks->upd_object = upd_object;
  hooks->del_object = del_object;
  // hooks->set_force = set_force;
  hooks->set_target_velocity = set_target_velocity;
  hooks->set_avatar_flying = set_avatar_flying;
  hooks->destroy = destroy_physics;
  hooks->apply_impulse = apply_impulse;

  phys->collisionConfiguration = new btDefaultCollisionConfiguration();
  phys->dispatcher = new btCollisionDispatcher(phys->collisionConfiguration);
  btVector3 worldMin(0,0,0);
  btVector3 worldMax(WORLD_REGION_SIZE,WORLD_HEIGHT,WORLD_REGION_SIZE);

  phys->overlappingPairCache = new bt32BitAxisSweep3(worldMin,worldMax,MAX_OBJECTS);

  phys->solver = new btSequentialImpulseConstraintSolver();
  phys->dynamicsWorld =  new btDiscreteDynamicsWorld(phys->dispatcher,
						     phys->overlappingPairCache,
						     phys->solver,
						     phys->collisionConfiguration);

  phys->dynamicsWorld->setInternalTickCallback(tick_callback, phys);
  phys->dynamicsWorld->setGravity(btVector3(0,-GRAVITY,0));

  float *heightfield = sim_get_heightfield(sim);
  // WARNING: note that this does *NOT* make its own copy of the heightfield
  // FIXME - this limits max terrain height to 100 metres
  phys->ground_shape = new btHeightfieldTerrainShape(WORLD_REGION_SIZE, 
						     WORLD_REGION_SIZE, 
						     heightfield,
						     0, 0.0f, 100.0f, 1,
						     PHY_FLOAT, 0);
  // scaling is a HACK to avoid a "falling off the edge of the world" bug
  phys->ground_shape->setLocalScaling(btVector3(1.004,1,1.004)); 
  btTransform ground_transform;
  ground_transform.setIdentity();
  ground_transform.setOrigin(btVector3(WORLD_REGION_SIZE/2.0f, 50,
				       WORLD_REGION_SIZE/2.0f
));
  
  btDefaultMotionState* motion = new btDefaultMotionState(ground_transform);
  btRigidBody::btRigidBodyConstructionInfo body_info(0.0,motion,phys->ground_shape,btVector3(0,0,0));
  btRigidBody* body = new btRigidBody(body_info);
  phys->dynamicsWorld->addRigidBody(body, COL_GROUND, GROUND_COLLIDES_WITH);


  // Sim edges - will need to selectively remove when region
  // crossing is added
  phys->plane_0x = add_sim_boundary(phys,1.0f,0.0f,0.0f);
  phys->plane_1x = add_sim_boundary(phys,-1.0f,0.0f,-WORLD_REGION_SIZE);
  phys->plane_0y = add_sim_boundary(phys,0.0f,1.0f,0.0f);
  phys->plane_1y = add_sim_boundary(phys,0.0f,-1.0f,-WORLD_REGION_SIZE);
  // TODO - add ceiling

  phys->shutdown = 0;
  g_static_mutex_init(&phys->mutex);

  phys->thread = g_thread_create(physics_thread, phys, TRUE, NULL);
  
  if(phys->thread == NULL) {
    printf("ERROR: couldn't create physics thread\n"); exit(1);
  }

  return TRUE;
}
