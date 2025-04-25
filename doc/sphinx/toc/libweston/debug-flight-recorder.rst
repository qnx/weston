.. _debugging flight recorder:


Debugging with Flight Recorder
==============================

Weston can write debug scopes data to a circular ring buffer. This ring
buffer can be accessed through a debug key, assuming you have a keyboard
attached, or in case Weston dies, through a coredump. This document describes
how to access that data in the later case.

The ring buffer data can be accessed with a gdb python script that searches
the coredump file for the that ring buffer address in order to retrieve
data from it.

Prior to setting this up make sure that flight recorder is configured
accordingly.  Make sure that Weston is started with the debug scopes that
you're interested into. For instance if you'd like to get the :samp:`drm-backend` one
Weston should show when starting up:

::

        Flight recorder: enabled, scopes subscribed: drm-backend

For that Weston needs to be started with :samp:`--debug -f drm-backend`.

Also, make sure that the system is configured to generate a core dump.  Refer
to :samp:`man core(5)` for how to do that.

Next you'll need the `gdb python
script <https://gitlab.freedesktop.org/wayland/weston/-/blob/main/doc/scripts/gdb/flight_rec.py>`_,
as that will be needed to search for the ring buffer within the coredump.

Finally, to make this easier and push everything from the ring buffer to a
file, we would need to create a batch gdb file script to invoke the commands
for us.

As an example name that file :file:`test.gdb` and add the following to entries
to it, making sure to adjust the path for the python script.

::

        source /path/to/flight_rec.py
        display_flight_rec

Then run the following commands to dump the contents of the ring buffer
straight to a file:

::

        $ gdb --batch --command=/path/to/test.gdb -q /path/to/test/weston/binary --core /path/to/coredump &> dump.log.txt
