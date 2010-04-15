// This is, effectively, a header file for the Cajeput LSL compiler
// It defines what built-in functions the compiler allows.

// bunch of core functions I need to implement. May be given opcodes at a later
// date.
vector llVecNorm(vector v) {}
float llVecMag(vector v) {}
float llVecDist(vector v1, vector v2) {}
// rotation llAxisAngle2Rot(vector axis, float angle) {}

llSay(integer channel, string message) { }
llShout(integer channel, string message) { }
llWhisper(integer channel, string message) { }
llOwnerSay(string message) { } // TODO

llResetTime( ) { }
float llGetTime( ) { }
float llGetAndResetTime( ) { }
llSetTimerEvent(float interval) { }
llSleep(float delay) { } // TODO

string llGetTimestamp( ) { } // TODO
integer llGetUnixTime( ) { }
float llGetGMTclock( ) { }

// float llFabs(float val) { }

//  llDetected* functions, not all implemented
integer llDetectedType( integer number ) { } // incomplete
string llDetectedName( integer number ) { }
key llDetectedKey( integer number ) { }
key llDetectedOwner( integer number ) { }
integer llDetectedGroup( integer number ) { }
vector llDetectedPos( integer number ) { }
rotation llDetectedRot( integer number ) { }
vector llDetectedVel( integer number ) { }
integer llDetectedTouchFace( integer number ) { }
vector llDetectedTouchST( integer number ) { }
vector llDetectedTouchUV( integer number ) { }

// TODO: llDetectedTouch*

// TODO - rest of llGetRegion* functions
vector llGetRegionCorner() { }
string llGetRegionName() { }

//osTeleportAgent( key avatar, integer region_x, integer region_y,
//		 vector pos, vector look_at) { }

osTeleportAgent( key avatar, string region,
		 vector pos, vector look_at) { }

string llGetObjectName() { }
string llGetObjectDesc() { }

// FIXME - how to implement this
float llFrand(float mag) { }

// FIXME - will be really fun to implement
integer llListen(integer channel, string name, key id, string message) { }
llListenRemove(integer listen_id) { }
llDialog(key avatar, string message, list buttons, integer chat_channel) { }
integer llListFindList(list src, list test) { }
list llParseString2List(string src, list seperators, list spacers) { }

// bunch of stuff I need to implement
llMessageLinked(integer linknum, integer num, string str, key id) { }
vector llGetPos() { }
rotation llGetRot() { }
vector llGetLocalPos() { }
rotation llGetLocalRot() { }
vector llGetRootPosition() { }
rotation llGetRootRotation() { }
key llGetKey() { }
string llGetScriptName() { }
integer llGetScriptState(string name) { }
llSetScriptState(string name, integer run) { }
integer llGetFreeMemory() { }
llResetScript() { }
llResetOtherScript(string name) { }
llSetRemoteScriptAccessPin(integer pin) { }
llRemoteLoadScriptPin(key target, string name, integer pin, integer running, integer start_param) { }
llSetTimerEvent(float interval) { }
llSetText(string text, vector color, float alpha) { }
llApplyImpulse( vector force, integer local ) { }
llSetPos(vector pos) { }
llSetRot(rotation rot) { }
llSetPrimitiveParams(list rules) { }
llSetLinkPrimitiveParams(integer link_num, list rules) { }
llSetLinkPrimitiveParamsFast(integer link_num, list rules) { }
string osGetSimulatorVersion() { }
