// This is, effectively, a header file for the Cajeput LSL compiler
// It defines what built-in functions the compiler allows.

// bunch of core functions I need to implement. May be given opcodes at a later
// date.
vector llVecNorm(vector v) {}
float llVecMag(vector v) {}
float llVecDist(vector v1, vector v2) {}
// rotation llAxisAngle2Rot(vector axis, float angle) {}
rotation llEuler2Rot(vector v) { } // TODO

llSay(integer channel, string message) { }
llShout(integer channel, string message) { }
llWhisper(integer channel, string message) { }
llOwnerSay(string message) { }
llInstantMessage(key user, string message) { } // TODO
llGiveInventory(key dest, string inventory) { } // TODO

llResetTime( ) { }
float llGetTime( ) { }
float llGetAndResetTime( ) { }
llSetTimerEvent(float interval) { }
llSleep(float delay) { }

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

integer llListen(integer channel, string name, key id, string message) { }
llListenRemove(integer listen_id) { }
llDialog(key avatar, string message, list buttons, integer chat_channel) { }

integer llListFindList(list src, list test) { }
list llParseString2List(string src, list seperators, list spacers) { }
list llParseStringKeepNulls(string src, list seperators, list spacers) { }


string llDumpList2String(list src, string seperator) { }
string llList2CSV(list src) { }

// FIXME - TODO. These will be a huge PITA.
list llListSort(list src, integer stride, integer ascending) { }
list llListReplaceList(list dest, list src, integer start, integer end) { }
list llDeleteSubList(list src, integer start, integer end) { }
list llList2List(list src, integer start, integer end) { }
list llList2ListStrided(list src, integer start, integer end, 
     integer stride) { }
list llListInsertList(list dest, list src, integer pos) { }

// TODO
string llGetSubString(string src, integer start, integer end) { }
string llDeleteSubString(string src, integer start, integer end) { }
integer llSubStringIndex(string src, string pattern) { }
string llToLower(string src) { }
string llToUpper(string src) { }
string llStringTrim(string src, integer how) { } 
string llInsertString(string dest, integer pos, string src) { }

// FIXME - TODO
integer llGetPermissions() { }

// FIXME - TODO (probably a big pain) - also check return type
key llHTTPRequest(string url, list parameters, string body) { }

// bunch of stuff I need to implement
llMessageLinked(integer linknum, integer num, string str, key id) { }
vector llGetPos() { }
rotation llGetRot() { }
vector llGetLocalPos() { }
rotation llGetLocalRot() { }
vector llGetRootPosition() { }
rotation llGetRootRotation() { }
integer llGetNumberOfPrims() { }
key llGetKey() { }
key llGetOwner() { }
vector llGetScale() { }
integer llGetAttached() {} // TODO
integer llSameGroup(key id) {} // TODO
float llGetAlpha(integer face) {} // TODO

key llGetLinkKey(integer linknum) { } // TODO

key llGetOwnerKey(key id) { }
list llGetObjectDetails(key id, list params) { } // TODO

key llGetLandOwnerAt(vector pos) { } // TODO
vector llGetAgentSize(key id) { } // TODO


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
llSetRot(rotation rot) { } // FIXME - check this is implemented right
llSetLocalRot(rotation rot) { } // TODO
llSetScale(vector scale) { }
llSetColor(vector color, integer face) { } // TODO
llSetLinkColor(integer linknum, vector color, integer face) { } // TODO
llSetAlpha(float alpha, integer face) { } // TODO
llSetLinkAlpha(integer linknum, float alpha, integer face) { } // TODO
llSetPrimitiveParams(list rules) { }
llSetLinkPrimitiveParams(integer link_num, list rules) { }
llSetLinkPrimitiveParamsFast(integer link_num, list rules) { }
string osGetSimulatorVersion() { }
string llKey2Name(key id) { }

// TODO
llRequestPermissions(key agent, integer perm) { }
integer llGetPermissions() { }
key llGetPermissionsKey() { }
llClearCameraParams() {}
llSetCameraParams(list rules) {}
vector llGetCameraPos() {}
rotation llGetCameraRot() {}
llStartAnimation(string anim) {}
llStopAnimation(string anim) {}
llPlaySound(string sound, float volume) {}