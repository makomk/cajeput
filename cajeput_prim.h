#ifndef CAJEPUT_PRIM_H
#define CAJEPUT_PRIM_H

#define PROFILE_SHAPE_CIRCLE 0
#define PROFILE_SHAPE_SQUARE 1
#define PROFILE_SHAPE_ISO_TRI 2
#define PROFILE_SHAPE_EQUIL_TRI 3
#define PROFILE_SHAPE_RIGHT_TRI 4
#define PROFILE_SHAPE_SEMICIRC 5

#define PROFILE_HOLLOW_DEFAULT 0x00
#define PROFILE_HOLLOW_CIRC 0x10
#define PROFILE_HOLLOW_SQUARE 0x20
#define PROFILE_HOLLOW_TRIANGLE 0x30

// both of these are stored in ProfileCurve
#define PROFILE_SHAPE_MASK 0xf
#define PROFILE_HOLLOW_MASK 0xf0

// FIXME - what are these
#define PATH_CURVE_STRAIGHT 0x10
#define PATH_CURVE_CIRCLE 0x20
#define PATH_CURVE_B 0x30
#define PATH_CURVE_FLEXI 0x80
#define PATH_CURVE_MASK 0xf0 // actually the only part of the param that's used

#endif
