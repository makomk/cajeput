This is Cajeput, a 3D virtual world server mostly compatible with OpenSim and
Second Life.

WARNING! This is very alpha software! Don't blame me if it eats your data, 
trashes your system, or murders your pets! (Seriously, you probably will get at
least a little data loss when using it right now.)

Compiling
---------
For now, Cajeput is only released as source code. In order to compile it, you
will need the following:

- A Linux or Unix system. Cajeput currently doesn't run on Windows.
- A working C++ compiler and toolchain.
- The development headers for the following packages:
  * libuuid
  * glib 2, minimum version unknown
  * json-glib 0.7.6 or later
  * libxml2 2.7.3 or later
  * SQLite 3.6.19 or later
  * libsoup 2.28.1 or later

Once you've got all that, run the following commands:
  cmake .
  make

Cajeput should be run from the build directory; installing it is not currently
supported. 

Getting Started
---------------

So, you've compiled Cajeput. Now you want to actually run it, which means 
configuring it. How to do this depends on whether you're running a standalone 
region or one connected to some external grid.

Connecting to a grid:

 Copy the sample config file server.ini.example to server.ini. You'll need to 
 fill in various details in the [grid] section:
  - The user server, grid server, inventory server and asset server URLs.
    If you're running an up-to-date local OpenSim grid server with the default 
    settings, you shouldn't have to change these. If connecting to a third-party
    grid, get these details from the grid's website or owner.
  - Which protocols the servers speak (new_userserver and use_xinventory).
    OpenSim 0.7 or later requires new_userserver=true and usually 
    use_xinventory=true too. OSGrid currently needs both to be set to false, 
    and other third-party grids probably require the same.
  - All the common details below.

Running a standalone region:

 Copy the sample config server.ini.standalone to server.ini, then fill in the
 common details as described below. You'll also need to add a user.
 [TODO]: Create an easy way of adding a user.

Common to both standalone and grid mode:

 Under [simgroup], you need to fill in:
  - The IP address of the region. If you're running a standalone region for 
    your own use, leave this at 127.0.0.1
  - The http port to use. Unless you're running another region or other 
    conflicting software, leave this at the default of 9000.
  - If you're running more than one sim, you need to edit the list of sims. 
    For example, if you have two sims with sections [sim foo] and [sim bar],
    put "sims=foo;bar"

 Under the [sim test] section for each sim, you need to:
  - Optionally replace "test" in the section header with a short name for the sim.
    If you're running more than one sim, each must have a different short name.
  - Set the UDP port for the sim. Again, unless you're running more than one sim, 
    the default UDP port of 9000 should work.
  - Set the position of the sim on the grid. The settings are multiplied by 256
    metres, so x=1000,y=1000 and x=1001,y=1000 are neighbouring regions.
  - Generate a *unique* UUID for the sim using the uuidgen command and add it
    to the config. Again, this must be unique for each sim! You may have to 
    install uuidgen.
  - Set the full name of the sim. Unlike the short name, this cannot be the 
    same as any sim anywhere on the grid, including sims run by other people.

You're now ready to start Cajeput. Cross your fingers and run ./cajeput_sim.
Hopefully it should start up OK. If you're running a standalone server, you 
should now set http://<your IP>:<your HTTP port>/ as the login URI. 
To shutdown Cajeput, press Ctrl-C *once* and wait. Pressing Ctrl-C again will 
kill Cajeput without saving any changes!
