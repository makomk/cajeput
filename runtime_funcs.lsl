// This is, effectively, a header file for the Cajeput LSL compiler
// It defines what built-in functions the compiler allows.

llSay(integer channel, string message) { }
llShout(integer channel, string message) { }
llWhisper(integer channel, string message) { }
llOwnerSay(string message) { } // TODO

llResetTime( ) { }
float llGetTime( ) { }
float llGetAndResetTime( ) { }

string llGetTimestamp( ) { } // TODO

// float llFabs(float val) { }

// bunch of stuff I need to implement
key llGetKey() { }
string llGetScriptName() { }
integer llGetScriptState(string name) { }
llSetScriptState(string name, integer run) { }
integer llGetFreeMemory() { }
llResetScript() { }
llResetOtherScript(string name) { }
llSetRemoteScriptAccessPin(integer pin) { }
llRemoteLoadScriptPin(key target, string name, integer pin, integer running, integer start_param) { }
