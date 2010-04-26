How to modify the Cajeput LSL runtime. Note that this doesn't cover the gory VM details; for those see HACKING.VM

Adding new LSL functions
------------------------

The first step in implementing a new LSL runtime function is to add a prototype for it to runtime_funcs.lsl. As you can see, it's basically defined in the same way as a normal LSL function, except it has no LSL code associated with it.

Next, you have to add some code in caj_scripting.cpp that implements the function. You have a choice: the code can either be run on the scripting thread (faster, but no ability to access any data outside of the scripting system) or on the main thread (slower, but it can interact directly with the rest of the simulator).

For example, suppose we're writing a function:
integer llFoo(integer bar, string baz)

Assuming we're running on the script thread, the function implementing this would have the standard template:

static void llFoo_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int bar; char* baz;
  vm_func_get_args(st, func_id, &bar, &baz);

  /* do some stuff */

  free(baz);
  vm_func_set_int_ret(st, func_id, retval);
  vm_func_return(st, func_id);
}

All implementations running on the scripting thread follow this standard form:
 - obtain the arguments using vm_func_get_args
 - carry out the actual operation
 - set the return value using vm_func_set_*_ret, if there is one
 - free any strings obtained from vm_func_get_args
 - and finally, call vm_func_return to return from the function call.

Function implementations running on the main thread are similar, but:
 - have a slightly different prototype and name (llFoo_rpc)
 - call rpc_func_return instead of vm_func_return
 - require the use of an additional macro RPC_TO_MAIN(llFoo, 0.0). This creates
   a function llFoo_cb that actually forwards the call to the main thread. The
   second argument describes how long (in seconds) the function call delays 
   execution of the script for

Finally, we need to hook up the new function to the script VM via an appropriate call to vm_world_add_func in caj_scripting_init:

vm_world_add_func(simscr->vmw, "llFoo", return type, llFoo_cb, 
		  number of arguments, argument type, argument type...);
