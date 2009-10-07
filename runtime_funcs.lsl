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

string llGetTimestamp( ) { } // TODO
integer llGetUnixTime( ) { }

// float llFabs(float val) { }

//  llDetected* functions, not all implemented
integer llDetectedType( integer number ) { } // ???
string llDetectedName( integer number ) { }
key llDetectedKey( integer number ) { }
key llDetectedOwner( integer number ) { }
integer llDetectedGroup( integer number ) { }
vector llDetectedPos( integer number ) { }
rotation llDetectedRot( integer number ) { }
vector llDetectedVel( integer number ) { }

// TODO: llDetectedTouch*

// bunch of stuff I need to implement
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
string osGetSimulatorVersion() { }
