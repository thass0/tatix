<h1 align="center">The Tatix System</h1>

The Tatix system is a custom kernel designed to serve static web pages. Tatix might intuitively
be called an operating system, but that would be unfair since the Tatix system lacks many
features of conventional operating systems. Tatix features a custom TCP/IP stack, an HTTP
server, a RAM file system, and concurrent tasks based on cooperative scheduling.
In support of these features, the system comes with drivers for hardware typical for x86 PCs,
a library with essential routines and data structures (allocators, strings, lists, buffers,
printing, formatting, etc.), and a paging implementation for virtual memory.

The system has been developed and tested in a virtual environment with QEMU on Linux. This
means it can easily be deployed on a Linux server that supports virtualization. Tatix depends on
GNU Make, NASM (assembler), GCC, QEMU (x86-64), iptables, Bash, GNU linker (ld), and Python 3.
By intention, this is standard tooling available on most Linux systems.

- [Using Tatix](#using-tatix)
- [Background](#background)
- [Internals](#internals)

# Using Tatix

Tatix can serve web pages over the global internet with HTTP. All that's required is a Linux
host with hardware virtualization and a public IP address.

In brief, the commands you need are:

```bash
export IF=eth0 # Change this to your outward-facing interface.
export DEBUG=2
./scripts/setup_vm_network.sh
make boot
```

Now the long version:

The contents of the `rootfs/` directory are archived and added to the kernel image before booting.
Tatix uses a custom archive format that's implemented by the `scripts/archive.py` script. The
contents of the `/hello.txt` file in the rootfs are printed when the system boots. The `/config.txt`
file is used to provide values for the runtime configuration. The default configuration works with
the virtual network set up by the `scripts/setup_vm_network.sh` script so there is no need to modify
it.

The `/web` directory (inside the rootfs) is the root of the web page that Tatix serves. This is where
the files and directories that should be served are. Supported file formats are HTML, plain text, CSS,
PNG, and JPEG. Extra formats can easily be added by extending `src/web.c` with the right MIME types.

N.B.: The size of the kernel image is statically configured to about 15&#8239;MB. This includes the rootfs
archive. The kernel itself is less than 1&#8239;MB in size, including static data. This sets an upper bound on
the size of pages Tatix can serve. (It's possible to lift this constraint by mapping more memory in
the `.entry` section and by changing the constants configured in `config.mk`. But there's no reason
for it because it's sufficient for small websites like mine.)

The `scripts/setup_vm_network.sh` script is used to configure a virtual network for the virtual
machine running Tatix. The script needs the `IF` environment variable to be set to an outward-facing
network interface to reach the internet. This would be something like `eth0` (the `ip addr` command
lists all available interfaces). The script sets up a TAP device for the VM and a bridge to connect
the TAP device to the interface `IF`. IP forwarding and forwarding of TCP and ICMP traffic destined
for the IP address of the VM are enabled, and NAT is configured to accept incoming connections. This
way, the Linux host acts as a router for Tatix.

There are two environment variables that control the build process. If the `RELEASE` environment
variable is set, compiler optimizations are turned on. The `DEBUG` flag controls the level of logs.
Possible values are:

- 0: Print warnings and errors. This is the default if `DEBUG` is not set.
- 1: Print informational messages.
- 2: Print debug messages.
- 3: Print everything (verbose).

A good default for development is `DEBUG=2` with `RELEASE` unset. For deployment, add `RELEASE=1`.

Now run `make boot` to build and start the system. On the machine running the VM, open `http://192.168.100.2`
in a browser to see the pages served by Tatix.

## Debugging

GDB can be used to debug Tatix by connecting to QEMU's GDB stub. To enable QEMU's GDB stub, set
`GDB=1` before running `make boot`. QEMU now halts execution and waits for GDB to connect.
Run `gdb build/kernel.elf` to start GDB with the kernel ELF. In GDB, run `target remote localhost:1234`.
This is to connect GDB to QEMU. From now on, debugging works as usual: set breakpoints and enter
`continue` to resume execution of the VM.

# Background

Tatix sprung from my interest in operating system development. I started working on the project thinking:
How hard can it be to write a bootloader? Next, I wanted to know how the basic hardware abstractions
(drivers, virtual memory, etc.) in an operating system work. Once I had a good baseline, I started looking
for something interesting to do with it. Serving static pages was a natural choice because it makes the
system usable, even for non-technical users, and instantly interesting.

Another motivation for writing Tatix was that I wanted to experiment with a style of C programming that
emphasizes correctness and readability by using different abstractions than those in the C standard library.
The style I landed on is heavily inspired by Chris Wellons' writing (see [nullprogram.com](https://nullprogram.com/)).
Its core elements are:

- [Fat strings](https://thasso.xyz/fixing-c-strings.html) made up of a pointer and a length instead
  of NULL-terminated strings
- Structured return values that are either a success with a value or an error with a code
- Heavy use of append-only buffers
- Different structures to represent mutable and immutable memory regions
- Arena allocators for short-lived storage

This style worked great for Tatix. E.g., I have yet to deal with bugs related to buffer overflows. It's also
easier to read and write code with these abstractions because they carry a lot of semantic information. I
recommend the ramfs (`src/ramfs.c`) and IP (`src/net/ip.c`) code to get an impression of this style.

I've been working on this project on and off for over a year. Two things have really helped me. First, having
the clear goal of serving web pages made it easier to make design decisions. Whenever I was stuck, I could
ask myself: Do I need this to serve a web page? If so, what's the simplest way to do it? Second, I learned a
lot about the internals of operating systems and about software development in general while working at Unikraft
last summer. This allowed me to figure out the rest myself.

# Internals

Below are descriptions of the internals of different parts of the Tatix system. They are intended
to document context on how everything fits together that can't be found in the code directly.

- [Boot procedure](#boot-procedure)
- [Tasks](#tasks)
- [Networking](#networking)
- [TCP implementation](#tcp-implementation)
- [RAM fs](#ram-fs)

## Boot procedure

The Tatix system uses the legacy BIOS boot process because it originated out of my attempts at
writing BIOS boot loaders. It's as simple as that. One could, of course, switch to UEFI, but BIOS
remains ubiquitous, so why bother? Using BIOS isn't the proper way of doing things, but it works
in this low-stakes environment.

The bootloader is made up of two stages. The first stage is very similar to a bootloader design
that I have [written about](https://thasso.xyz/setting-up-an-x86-cpu.html) at length on my blog.
The entire boot sector is contained in `bootloader/bootloader.s`. It uses BIOS in 16-bit mode to
load a second stage written in C into memory (`bootloader/load_kernel.c`). The boot sector code
also sets up 64-bit long mode with paging and a GDT. The paging code uses 1&#8239;GB huge pages for
simplicity. These paging and GDT setups are temporary and are replaced later on.

The second stage of the bootloader implements a rudimentary IDE disk driver to load the kernel
ELF from disk into memory. Then, it calls the entry point of the kernel ELF.

At this point, the first 4&#8239;GB of virtual memory are identity-mapped to the first 4&#8239;GB
of physical memory. The entry point of the kernel ELF can be found in the `.entry` section which
is linked for identity-mapped memory. The code in the `.entry` section sets up the second temporary page
table. This time, only 32&#8239;MB of physical memory are mapped, which is enough for the first stages
of the kernel initialization procedure to run on. Two different ranges of virtual addresses are
mapped to the first 32&#8239;MB of physical memory: one is an identity mapping, and the other is a
linear mapping that starts at the base virtual address `KERN_BASE_VADDR`, as configured in `config.mk`.
The identity mapping is required so the code in the `.entry` section continues to work when switching
to the new page table. The mapping starting at `KERN_BASE_VADDR` is required because the main kernel
uses the virtual addresses starting there.

Why does the code in the `.entry` section only map 32&#8239;MB of memory instead of the original 4&#8239;GB?
The page tables in the `.entry` section are statically allocated, and we don't want to waste
a lot of memory on them (it can't be repurposed). So, we use the bare minimum here.
The 32&#8239;MB cover the kernel image (which is statically allocated to be about 15&#8239;MB at maximum,
cf. `config.mk`) and the first few megabytes of the region of dynamically managed memory. The latter is
needed to allocate page table pages when finally initializing paging later on.

From the `.entry` section, control is passed to `kernel_init`. Now, the code that's being
executed is running in high memory, i.e., it's no longer identity mapped. Really, there is
no reason to do this since there are no user space programs in the Tatix system. In fact,
the paging code could be removed without fundamentally breaking anything. At this point,
it's a leftover from my early experimentation before I decided I only want to serve web
pages and not execute user space programs.

The `kernel_init` function calls the initialization functions of all kernel subsystems that
need initialization. The order is important here because the different subsystems depend on
each other. If any of the initialization functions fail, the system crashes because each step
in the initialization procedure is essential.

At the end, the `kernel_init` function passes control to the `main` function. Here, the different
tasks running on the Tatix system are set up, and we start executing them.

## Tasks

Tatix runs on a single CPU core and uses a cooperative scheduler to allow writing concurrent code.
Networking code is, by nature, interleaved because control switches between receiving and sending
data and processing data in between. This makes the scheduler essential.

None of the networking code interacts with the scheduler directly. Instead, there are tasks
defined in `main` that call into the networking code. The two most important tasks are
`task_net_receive` and `task_tcp_poll_retransmit`. The former polls the network device for
incoming packets and passes them up the network stack to the right protocol handlers. The
latter calls into the TCP subsystem to update the retransmission queues and trigger
retransmissions (see below).

There is a third task that uses ICMP to send a ping to Google's 8.8.8.8 nameserver. This is just
a check to see if we can reach the internet.

A fourth task runs the web server. The web server uses the TCP subsystem to accept incoming
connections on port 80 and to handle HTTP requests. It also uses the scheduler to yield control
when it's waiting for new TCP traffic to arrive. After yielding control, the `task_net_receive`
task can run to receive data, which is passed up to the TCP subsystem. Once that has happened,
the web server can start to handle the request.

The web server is written to be reentrant so that there can exist multiple tasks running
the web server at the same time. This achieves better performance than a single web server
task because, if one task is waiting for data to arrive, another can run.

No locking or other synchronization is required for the system to work. By the nature of a
cooperative scheduler, code is never interrupted involuntarily, so we carefully choose where
to give up control. Subsystems that use global data structures (e.g., the virtual memory
allocator `kvalloc` or the TCP subsystem) just don't call into the allocator. In a way,
this is a method of synchronizing access on its own. Additionally, since Tatix runs on a
single core, there is no need to protect global data against parallel access from multiple
cores.

## Networking

At the lowest level, there is the e1000 driver, which is a driver for Intel 8254x NICs, the
standard model in QEMU. The device is configured to fire an interrupt when new data has arrived.
The interrupt handler copies the data from the NIC into a queue in the `netdev` subsystem, which
is an abstract wrapper around the e1000 driver. Access to the queue is protected by disabling
interrupts. For sending data, the NIC is polled until its internal queues have enough space.
Then the outgoing data is copied to the NIC and eventually transmitted.

The receive task (see above) calls into the `netdev` subsystem to check if there are packets
waiting in the queue. If so, the receive task switches on the protocol number of the packets
and passes them up the network stack for processing. At each step, the correctness of the
data at the current layer is checked, and the data is demultiplexed until the top protocol
layer is reached. Responses are transmitted for the first time right when handling the
incoming data (in TCP, the responses are also added to the retransmission queue).

A simple routing table is configured based on the `/config.txt` file in the rootfs. This
happens inside `kernel_init`. To transmit data at the IP layer, the route is determined
based on the destination IP address. If the MAC address for the gateway IP address of the
route isn't yet known, an ARP request is transmitted instead of the IP packet.

## TCP implementation

The TCP subsystem is the most complex so this section features the most detail. It's highly
instructive to read the relevant RFCs to understand the TCP implementation (copies in the
`manuals/` directory):

* [RFC 1323: TCP Extensions for High Performance](https://www.rfc-editor.org/rfc/rfc1323)
* [RFC 6298: Computing TCP's Retransmission Timer](https://www.rfc-editor.org/rfc/rfc6298)
* [RFC 9293: Transmission Control Protocol (TCP)](https://www.rfc-editor.org/rfc/rfc9293)

### User interface

These are the essential functions exposed by the TCP subsystem:

```c
struct tcp_conn *tcp_conn_listen(struct ipv4_addr addr, u16 port, struct arena tmp);
struct tcp_conn *tcp_conn_accept(struct tcp_conn *listen_conn);
struct result_sz tcp_conn_send(struct tcp_conn *conn, struct byte_view payload, bool *peer_closed_conn,
                               struct arena tmp);
struct result_sz tcp_conn_recv(struct tcp_conn *conn, struct byte_buf *buf, bool *peer_closed_conn);
struct result tcp_conn_close(struct tcp_conn **conn, struct arena tmp);
```

There is only one user of the TCP interface, really. It's the web server. That's why the TCP
API only supports the server side of things. Conceptually, this is how you set up a TCP server
with Berkeley Sockets:

```c
sfd = socket(AF_INET, SOCK_STREAM, 0);
bind(sfd, (struct sockaddr *)&addr, sizeof(addr));
listen(sfd, BACKLOG_SIZE);
```

The effect of all three calls is implemented by `tcp_conn_listen` in the Tatix TCP interface,
which takes as arguments an IP address and a port number. `tcp_conn_listen` returns a connection
structure that serves as a handle for a `LISTEN`-state connection ("listen connection" for short)
that the function creates. This connection structure essentially serves the purpose of the
`sfd` file descriptor in the example above.

Connections can be accepted with `tcp_conn_accept`. The only argument to `tcp_conn_accept` is
a listen connection. If a peer has tried to establish a connection with the right IP address
and port before `tcp_conn_accept` is called, a `struct tcp_conn` handle for this connection is
returned by `tcp_conn_accept`. Here, the "right" IP address and port number are, of course, the
IP address and port number that were passed to `tcp_conn_listen`. `tcp_conn_accept` can be polled
to await a connection.

Note that `tcp_conn_listen` and `tcp_conn_accept` both create a new connection. I.e., after
calling each function once and getting a non-`NULL` return value both times, there exist two
connections: one in the `LISTEN` state and one representing an active connection to a peer. This,
in turn, means a listen connection can be reused indefinitly to accept further connections. The
listen connection is deleted only after calling `tcp_conn_close` on it.

Three operations can be performed on open connections returned by `tcp_conn_accept`: sending data
to the peer, receiving data from the peer, and closing the connection. Connections are closed and
deleted by `tcp_conn_close`.

The send and receive functions can be called arbitrarily often. Let's start with send.
The `tcp_conn_send` function takes as arguments a connection, a payload of bytes, and a pointer to
a boolean flag to indicate if the peer has closed the connection. If the peer has closed the
connection, it won't acknowledge any new data. Check this flag periodically
and close the connection if it's set. The return value of `tcp_conn_send` indiciates the number
of bytes that were transmitted. TCP uses a sliding-window approach to traffic control. If the
user of the TCP API sends too much data too quickly, the window will fill up. In that case, the
TCP implementation stops transmitting, and the return value of `tcp_conn_send` is smaller than the
length of the payload.

The `tcp_conn_send` function internally splits the payload that it's given into fragments small
enough to fit one Ethernet frame. It's possible to transmit TCP segments larger than this by relying
on fragmentation at the IP layer. However, this has a downside. As TCP retransmits data at the
segment level, if a single IP fragment in a large segment is lost, the entire segment
must be retransmitted. This leads to considerable overhead. We circumvent the issue by fragmenting
at the TCP level. After splitting the payload, each fragment is transmitted for the first
time right away in the `tcp_conn_send` call. The fragments are also added to the send buffer
queue (SBQ) of the connection for retransmission (see below).

The `tcp_conn_recv` function takes as arguments a connection and a buffer to store the received
data in, as well as the flag to indicate if the peer has closed the connection. All data received
from the peer is buffered internally by the TCP implementation. When calling `tcp_conn_recv`, the
available data is copied from the internal buffer into the buffer passed to `tcp_conn_recv`. The
amount of data that's copied is limited by the amount available in the internal buffer and by
the size of the buffer passed to `tcp_conn_recv`. The number of bytes that were copied is returned.
This means `0` is returned if no data is available (assuming the buffer passed to `tcp_conn_recv`
has some space).

Another note on the flag that indicates if the peer has closed the connection is in order. The
Berkeley Sockets API returns `-EOF` from `read(2)` calls if the connection was closed. This ties
in nicely with the behavior of `read(2)` on regular files, but it means packing error codes into
the negative range of naturally non-negative return values, an ancient practice that is rejected
in the design of the Tatix system. Opting for a flag instead is natural, as the condition in question
("Has the peer closed the connection?") doesn't need to be checked on every call; it just has to be
checked eventually to avoid infinite loops.

### The TCP state machine

The TCP protocol is based on a per-connection state machine. RFC 9293 contains an ASCII
diagram of the different states (there is a graphical version in the
[`manuals/TCPIP_State_Transition_Diagram`](../manuals/TCPIP_State_Transition_Diagram.pdf) PDF):

```plain
                            +---------+ ---------\      active OPEN
                            |  CLOSED |            \    -----------
                            +---------+<---------\   \   create TCB
                              |     ^              \   \  snd SYN
                 passive OPEN |     |   CLOSE        \   \
                 ------------ |     | ----------       \   \
                  create TCB  |     | delete TCB         \   \
                              V     |                      \   \
          rcv RST (note 1)  +---------+            CLOSE    |    \
       -------------------->|  LISTEN |          ---------- |     |
      /                     +---------+          delete TCB |     |
     /           rcv SYN      |     |     SEND              |     |
    /           -----------   |     |    -------            |     V
+--------+      snd SYN,ACK  /       \   snd SYN          +--------+
|        |<-----------------           ------------------>|        |
|  SYN   |                    rcv SYN                     |  SYN   |
|  RCVD  |<-----------------------------------------------|  SENT  |
|        |                  snd SYN,ACK                   |        |
|        |------------------           -------------------|        |
+--------+   rcv ACK of SYN  \       /  rcv SYN,ACK       +--------+
   |         --------------   |     |   -----------
   |                x         |     |     snd ACK
   |                          V     V
   |  CLOSE                 +---------+
   | -------                |  ESTAB  |
   | snd FIN                +---------+
   |                 CLOSE    |     |    rcv FIN
   V                -------   |     |    -------
+---------+         snd FIN  /       \   snd ACK         +---------+
|  FIN    |<----------------          ------------------>|  CLOSE  |
| WAIT-1  |------------------                            |   WAIT  |
+---------+          rcv FIN  \                          +---------+
  | rcv ACK of FIN   -------   |                          CLOSE  |
  | --------------   snd ACK   |                         ------- |
  V        x                   V                         snd FIN V
+---------+               +---------+                    +---------+
|FINWAIT-2|               | CLOSING |                    | LAST-ACK|
+---------+               +---------+                    +---------+
  |              rcv ACK of FIN |                 rcv ACK of FIN |
  |  rcv FIN     -------------- |    Timeout=2MSL -------------- |
  |  -------            x       V    ------------        x       V
   \ snd ACK              +---------+delete TCB          +---------+
     -------------------->|TIME-WAIT|------------------->| CLOSED  |
                          +---------+                    +---------+
```

The Tatix TCP implementation adheres to these states closely and is modeled according to them.
When a segment is received from the IP layer, the corresponding connection is looked up, and a
call into a handler based on the current state of the connection is made. These handlers
manage the connection: they handle  transitions between states, allocate and free connections
and receive buffers, and update the different variables in the connection structures. Their
function names start with `tcp_handle_receive_`.

This way of doing things---handling each state separately---is, arguably, a bit verbose. It's
certainly possible to coalesce similar behavior into a generic handler that treats the
idiosyncrasies of different states as special cases. The upside, however, of being so
verbose is that the code of the state handlers clearly reflects the behavior specified in
the RFCs. It's easy to understand, verify, and debug. This trumps the benefits of condensing the
behavior into fewer lines. Plus, in obvious cases, common behavior is already factored into
the `tcp_conn_update_*` functions.

### Reception and circular receive buffers

A TCP connection can receive data when it's in the `ESTABLISHED` state. There is a task in the
Tatix system that polls the network device for data. If IP data is available, it's passed to
the IP layer, which, in turn, passes the data to the `tcp_handle_packet` function after
extracting the TCP/IP pseudo header. The `tcp_handle_packet` function invokes the state
handlers described above.

When data is received by an `ESTABLISHED` TCP connection, it's appended to a circular buffer.
The buffer is allocated right before transitioning the connection to the `ESTABLISHED` state,
and it has a fixed size. The TCP implementation advertises the amount of available space to
the peer with the window size field of the TCP header. The advertised window size decreases
while the circular buffer fills up, which discourages the peer from sending more data. The
window size increases again after the user of the TCP subsystem has copied the received data
out of the circular buffer.

### Send buffer queues (SBQs) and retransmissions

A key function of TCP, besides traffic control, is to ensure delivery of data. Here is how
Tatix implements it.

The `tcp_conn_send` function of the TCP API calls into the internal `tcp_send_segment` function.
The latter allocates a new send buffer (SB), a data structure that stores outgoing data in a way
that makes it easy to prepend protocol headers while moving down the network stack. The payload
passed to `tcp_send_segment` is copied to the newly allocated send buffer and the send buffer
is added to the send buffer queue (SBQ) of the current connection. The SBQ is simply a linked list
of send buffers with some metadata attached to each send buffer. Most notably, each node in the
SBQ contains time stamps to time retransmissions and the acknowledgment (ACK) number that has to
arrive before the segment can be removed from the retransmission queue (i.e., the SBQ).

The `tcp_send_segment` function directory calls into the IP layer to transmit the segment for the
first time after adding the payload to the retransmission queue. There is a task in the Tatix system
that periodically calls into the TCP subsystem with `tcp_poll_retransmit`. This function iterates
over all active connections and their SBQs. Each node in the SBQ is processed as follows (where
one node represents one segment waiting for retransmission or acknowledgment):

1. If the ACK for the segment has arrived since the last poll, the segment data is freed
   and the node is removed from the queue.
2. If a maximum number of retransmission attempts has been reached, the segment data is freed
   and the node is removed from the queue.
3. If neither of the two above conditions holds, and the retransmission timeout of the segment has
   expired, it's transmitted again. The timeout before the next retransmission is doubled each time
   a segment is retransmitted (exponential backoff).

The time stamps option (TSopt) is used by the Tatix system to measure the round-trip time
(RTT) of a connection. The base retransmission timeout (RTO) is computed dynamically based on RTT
measurements. If TSopt isn't present, a default RTO of 1 second is used. (This will never
be the case because TSopt was introduced in 1992 and is essential to modern TCP.) The RTO
is computed with the algorithm in RFC 6298.

### Memory management and allocators

In TCP, there is frequent need for new connection structures and data buffers, most of which are
short-lived. The Tatix TCP implementation uses two different allocation strategies.

For one, there is a big global array of connection structures. Each one has a flag that tracks
if the structure is used. A new connection is allocated by searching the array for an unused
structure and returning a pointer to this structure. This is simple and fast. It also makes for
good data locality since all connection structures are at the same place in memory.

A connection control structure is allocated from the connection array right when the TCP
handshake with a peer starts. But no data buffers are allocated at this point; it's just a few
integers. A receive buffer for the connection is allocated after the TCP handshake is completed.
All receive buffers are the same size and allocated from a common pool allocator. The pool allocator
too has a low overhead (both space and time) and provides strong data locality. The reason a pool
allocator isn't used for the connection structures is that frequent scans over all active connections
are required. This is an operation a pool allocator isn't specifically designed for, while the
array approach naturally lends itself to it.

The send buffers of the retransmission queue (SBQ) are allocated from their own pool allocator. Any
number of send buffers can be allocated for a single connection, depending on how much data the user
of the TCP subsystem is transmitting and how much of it is kept around unacknowledged. Send buffers
are freed based on the rules above.

The pool allocators, in turn, are backed by big contiguous allocations from `kvalloc`. All of them
are allocated at boot. This strategy leads to low fragmentation and speedy allocations.

## RAM fs

Tatix features a RAM file system (ramfs) whose primary purpose is to store the content served by the
web server. The essential structure of the ramfs is the `struct ram_fs_node`. Here's how it looks:
```c
struct ram_fs_node {
    // First node in the directory if this node is of type RAM_FS_TYPE_DIR.
    struct ram_fs_node *first;
    // Next node in the same directory as this node. A linked list.
    struct ram_fs_node *next;
    enum ram_fs_node_type type;
    struct str name;
    // Data of the file if this node is of type RAM_FS_TYPE_FILE.
    struct byte_buf data;
    // Pointer back to parent FS.
    struct ram_fs *fs;
};
```

An instance of a ramfs is defined by the `struct ram_fs`:

```c
struct ram_fs {
    struct alloc data_alloc;
    struct pool node_alloc;
    struct arena scratch;
    struct ram_fs_node *root;
};
```

The `data_alloc` is an abstract allocator (could be any) that's used to allocate buffers for file
data and the names of nodes. The `node_alloc` is a pool allocator that hands out fixed-size chunks
of memory for `struct ram_fs_node` allocations. A ramfs is created by calling `ram_fs_new`. This
function takes the `data_alloc` as its only argument. The `node_alloc` is then allocated from the
`data_alloc`.

A separate allocator for the names of nodes would make sense because the allocation patterns
of the names and the data buffers are quite different. But names vary in size, so a pool allocator
would waste lots of memory (each allocation would have to be the maximum size). So, instead of
adding an extra allocator, we use the `data_alloc` for all allocations of varying lengths.

Now back to `struct ram_fs_node`. With the `first` and `next` fields of the structure, a tree structure
is maintained. The `next` field is a linked list of all nodes in the same directory. The `first` field is
only set if a given node is a directory. It points to the first node in the directory. Subsequent
nodes in the directory can be found by following the `next` pointer.

```
+-ram_fs_node-----------+
|                       |
| /web                  | NULL
|                       |
+---+-------------------+
    |
    | first
    v
+-ram_fs_node-----------+                   +-ram_fs_node-----------+
|                       | next        next  |                       |
| /web/index.html       +------> ... ------>| /web/public           | NULL
|                       |                   |                       |
+-----------------------+                   +---+-------------------+
   NULL                                         |
                                                | first
                                                v
                                            +-ram_fs_node-----------+
                                            |                       |
                                            | /web/public/style.css | NULL
                                            |                       |
                                            +-----------------------+
                                               NULL
```

Internally, paths are represented by the `struct path_name` structure. It looks like this:

```c
struct path_name {
    struct str src;
    // The path '/' is represented by a `struct path_name` where `n_components` is 0, the empty path.
    sz n_components;
    struct str *components;
    bool is_absolute;
};
```

The `path_name_parse` function creates a `struct path_name`. It takes a string (the path name to
parse) and an arena allocator as its arguments. The `path_name` structure is meant to facilitate easy
access to the components of a path name string. It works like this: The `src` field holds a full,
unmodified copy of the path name string, allocated from the arena. The `n_components` and `components`
fields are an array of string slices. The array is also allocated from the arena, but each string structure
in the array is a slice pointing into the `src` string. The component slices don't include any slashes.

With this structure in hand, it's trivial to do a path lookup. This way, path parsing and path lookup
are disentangled, which makes the code easy to understand and verify (I've seen both done in the same
function, and it's not pretty).
