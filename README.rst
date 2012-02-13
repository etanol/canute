TABLE OF CONTENTS
=================

1. About Canute
2. Usage
3. Compilation
   #. *Hasefroch*
4. Protocol enhancements
   #. File modification time
   #. Executable bit
5. Source code files
6. Credits


1. ABOUT CANUTE
===============

Canute is a small command line utility to transfer files and directories over
the network.  It does not have authentication nor any other kind of privileges.
It can be handy in a number of situations, specially on LAN.

For example, imagine you are with a friend and he has a big CD/DVD image you
want (whatever Linux distro).  Depending on the operating systems you have,
there are some ways of doing the copy:

- **FTP:** Set up a server (you need root privileges for that) and then allow
  anonymous upload, create a new account for your friend or give him access
  using your account.  Don't forget a good FTP client

- **HTTP:** Maybe handier than FTP but upload is a bit trickier.

- **SFTP/SSH:** This is account based so give new accounts or your own.

- **RSync:** If you want to use it two way you need to transfer over SSH, then you
  are in the same situation as before.  Otherwise both of you should configure a
  server with the appropriate modules (which, again, requires root privileges).

- **NFS:** *Hasefroch* does not easily support it.  This is not as hard as it used to
  be thanks to the modern Linux distributions.  But you need at least two
  servers: the port mapper and the nfsd.  Not mentioning the kernel support, the
  permission grant from the server (/etc/hosts.allow), etc...  And it is quite
  slow for big files.

- **SMB:** Very similar to NFS but slower.

- Finally, you could also waste a CD/DVD burning that ISO image and giving it to
  your friend.

With Canute you use the same binary for all tasks: sending or receiving.  No
need for installation, just the damn binary.


2. USAGE
========

There are two pairs of concepts: server-client and sender-receiver.  The
server-client couple only defines who (of the two peers) has to execute the
application first.  The sender-receiver defines the direction of the
transfer.

Let's have host *A* and host *B*.  The following scenarios show the commands and the
order in which must be executed:

**Scenario 1**
   *A* sends files to *B*.  This is the usual way of working. ::

      host_A$ canute send file1 file2 ...
      host_B$ canute get host_A

**Scenario 2**
   *A* sends files to *B*, but *A* is behind a firewall and cannot open ports
   (NAT/PAT). ::

      host_B$ canute getserv
      host_A$ canute sendto host_B file1 file2 ...

We see how server-client roles can be switched independently from
sender-receiver roles.  The server is the peer who waits for the client to be
connected.  The server is not persistent and the port is closed when it
finishes.  That means you do not have to worry about leaving that port opened on
your router if you want to do Internet transfers.

You can also choose a different port than the default (1121/tcp) specifying it
after the Canute sub-command, like this::

   host_A$ canute send:5030 file1 file2 ...
   host_B$ canute get:5030 host_A

Or else::

   host_B$ canute getserv:5030
   host_A$ canute sendto:5030 host_B file1 file2 ...

When a directory path is provided as a command line argument, then is sent
recursively.


3. COMPILATION
==============

Canute can now compile in many UNIX flavours, as well as in *Hasefroch* natively
(both 32-bit and 64-bit) using MinGW.

The Makefile is written using some GNU Make extensions (beware BSD users, do not
forget to use ``gmake``).  If you are compiling within a GNU environment (GCC), as
many open source Unices are, you do not need to do any special tricks.  Execute
``make help`` to find out what you need.

Commercial UNIX variants do not offer a GNU environment, mainly because they
have their proprietary C compiler, therefore compiler flags must be modified.
However, some commercial UNIX flavours are supported as long as GNU Make is
installed.

In particular, to compile Canute in Solaris (SunOS 5.x) and in HP-UX, it is
enough to run ``gmake``.  Note that OSF1 (5.1 and above) testing has been
dropped because we do not have access to any such platform anymore.  Other
flavours (like AIX) may need additional tuning.  Porting patches are welcomed.


3.1 Hasefroch (aka: Win32)
--------------------------

As a *Hasefroch* average user, you are not expected to be interested in this part
because we already provide a binary.  But if you would like to patch Canute
yourself and produce binaries for *Hasefroch* you can easily do so from UNIX by
doing cross compilation.  Install MinGW (crossed) from your package manager and
execute::

   make hase

If you want to do it in *Hasefroch*, get GNU Make from `Unix tools for Win32`__
and MinGW.  Tune up a bit the ``Makefile`` and compile as you like.  The
``Makefile`` is very straightforward.

__ http://unxutils.sourceforge.net


4. PROTOCOL ENHANCEMENTS
========================

Version 1.2 introduces some improvements on the protocol.  The previously
reserved field in the header packet now carries some additional information
about the file being transferred.

To maintain backwards compatibility with versions 1.0 and 1.1, the last byte of
each file header is marked with a special value.  Old versions will ignore that
mark and the new information introduced into the header packet.

So if one of the peers is using a version below 1.2, do not expect these added
features to work.


4.1. File modification time
---------------------------

Most remote copying utilities provide a way to maintain, at least, file
modification time intact.  In general it is desirable to preserve such
information for many reasons; for example, to make a backup-like remote copy.

An attempt to use this information to decide upon resuming transfers showed that
it is trickier than it seems at a first glance.  Therefore, the resume policy
has not been altered.

Remember, though, that Canute is NOT a mirroring nor backup software.
Nevertheless, this feature helps to make Canute friendlier to such mirroring
software.


4.2. Executable bit
-------------------

The Canute experience has also shown that in many cases, the lack of executable
bit information introduces some inconveniences.  This is particularly annoying
on large directory tree transfers.  Therefore, this information is now sent
through the connection.

The executable bit is also kind of "resumed" because it propagates.  But the
lack of it does NOT propagate.  That means once the executable bit is set
locally, subsequent resumes will not clear it.

Obviously, the use of executable bit is disabled in *Hasefroch* builds as it
does not make sense.


5. SOURCE CODE FILES
====================

:``canute.h``:
   Dirty tricks to make the rest of the code portable and as ``#ifdef`` clean as
   possible.

:``canute.c``:
   Main function.  Command line parsing and role selection (server-client,
   sender-receiver).

:``feedback.c``:
   User feedback module, progress bar, information and timing.

:``net.c``:
   Basic network management functions.  Connection handling, block transfer and
   message passing.

:``protocol.c``:
   Sender-receiver negotiations and content transfers.

:``util.c``:
   Unclassified utility functions.


6. CREDITS
==========

:Original idea and current maintenance: C2H5OH
:Major contributions and ideas: MKD_
:Initial win32 port: Plimo San
:Testing aid: MKD_, Tito Houzy, m3gumi, bl4d3

.. _MKD: http://www.claudiocamacho.com
