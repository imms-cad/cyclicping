# Cyclicping

Tool for measuring communication latency (round trip time) between two (RTPreempt) Linux systems.

Currently supported protocols (interfaces) are:

- UDP
- TCP
- Uart
- TSN (experimental)
- [Netmap](https://github.com/luigirizzo/netmap) (experimental)

## Building

Just call make:

`make`

If you need Netmap support, set NETMAP accordingly:

`make NETMAP=<Netmap Path>`

Crosscompiling can be done by settings CC to your cross compiler:

`make CC=<your cross compiler>`

## Usage

`cyclicping <options>`

* `-2, --two-way`

	If server and client are time syncronized (for example by using PTP), this option can be used to collect additional send and receive time statistics. The accuracy of send and receive duration depends of course of the accuracy of the the time syncronization. Both server and client have to specify this option.
* `-a <nr>, --affinity <nr>`

	Sets the CPU affinity of cyclicping. This specifies the CPU cyclicping will run on (not a mask).
* `-b <threshold>, --breaktrace <threshold>`

	Stop a running ftrace if packet latency exceeds threshold.
* `-c, --client`

	Run in client mode.
* `-C <clock>, --clock <clock>`

	Select the clock cyclicping uses for timestamping (0 for MONOTONIC, 1 for REALTIME). MONOTONIC is used as default. REALTIME will be selected in two-way mode.
* `-d <file>, --dump <file>`

	Timestamps for every packet will be dumped to file. (Timestamps are cached in memory. Long running traces might exceed the available amount of memory.)
* `-f, --ftrace`

	Start a kernel function trace (function_graph) during cyclicping run time. (Kernel ftrace support has to be enabled. Debug fs has to be mounted under /sys/kernel/debug).
* `-g, --gnuplot`

	Append [Gnuplot](http://www.gnuplot.info/) script code to histogram data. Saved to a file or piped to Gnuplot this allows generating a histogram plot. Gnuplot version > 5.0 is required for this to work.
* `-h, --help`

	Print usage information.
* `-H <size>, --histogram <size>`

	Collect and print histogram data.
* `-i <time>, --interval <time>`

	Set the packet interval time. Default unit is us if not changed via -M option.
* `-l <packets>, --loops <packets>`

	Packet number cyclicping will send before aborting. Default is to run forever.
* `-L <size>, --length <size>`

	Packet payload size in bytes cyclicping will use (Default: 64).
* `-m, --mlockall`

	Lock process memory.
* `-M, --ms`

	Use milliseconds as time base instead of microseconds.
* `-p <priority>, --prio <priority>`

	Process priority cyclicping will use.
* `-P <priority>, --so-prio <priority>`

	Network socket priority cyclicping will set for socket based modules.
* `-q, --quit`

	Be less verbose and don't output current statistics.
* `-s, --server`

	Run in server mode.
* `-t <tos>, --tos <tos>`

	Sets the [TOS](https://en.wikipedia.org/wiki/Type_of_service) or DSCP field in the IP header if using IP based modules. For example using `-t 160` will set the field to `0xa0` indicating class 5 traffic. Client and server are using individual values.
* `-u <module:config>, --use <module:config>`

	Use interface module for measuring (see table below).
* `-v, --verbose`

	Be more verbose
* `-V, --version`

	Print cyclicpings version.

The following interface modules are available:


Module | Server | Client
--- | --- | ---
UDP | `udp[:port]` | `udp:serverip[:port]`
TCP | `tcp[:port]` | `tcp:serverip[:port]`
UART | `uart:device[:baud[:flow]]` | `uart:device[:baud[:flow]]`
TSN* | `stsn:interface:clientmac` | `stsn:interface:servermac`
Netmap* | `netmap:interface[:port]` | `netmap:interface:servermac:serverip[:port]`

\* Server mac address has to be given using '-' as separator

## Data Output

If nothing else is specified, cyclicping will print out the collected round trip data statistics, showing current, average, minimum and maximum RTT.

Using the `-H <size>, --histogram <size>` option, cyclicping will collect and print out a histogram of the round trip time. Use the `-q, --quit` option for better piping this data to another program or forwarding it into a file.

Adding `-g, --gnuplot` makes cyclicping print out additional Gnuplot script code before the actual histogram data. This allows plotting the histogram directly.

## Examples

* TCP live statistic, send a packet every ms

	Server: `./cyclicping -s -u tcp -i 1000`

	Client: `./cyclicping -c -u tcp:<serverip>`

* Use USB-UARTs with 460800 baud on server and client (connected with a crosslink cable). Send 100000 packets. Packet interval is 1ms. Generate a histogram of round trip times and store it in the file hist.txt.

	Server: `./cyclicping -s -u uart:/dev/ttyUSB0:460800`

	Client: `./cyclicping -c -u uart:/dev/ttyUSB0:460800 -i 1000 -l 100000 -H 2000 -q > hist.txt`

* TSN live statistic. Here link layer packets with the Ethernet Protocol ID set to TSN (0x22F0) are used. Socket priority gets set to 3, process priority to 70 and cyclicping is pinned to CPU #0. Stream reservation, configuring for example a credit based shaper, making sure packets from cyclicping are using the correct send queues, etc. has do be done separately. A script (cbs-conf.sh) is provided to for example setup the newly proposed CBS. But this requires the [CBS patches](https://www.spinics.net/lists/netdev/msg460869.html) applied to the kernel and also a patched [iproute2](https://www.spinics.net/lists/netdev/msg459554.html).

	Server: `./cyclicping -s -p 70 -P 3 -a 0 -u stsn:enp3s0:a0-36-9f-e0-2e-ee`

	Client: `./cyclicping -c -p 70 -P 3 -a 0 -u stsn:enp4s0:a0-36-9f-e0-2d-d5 -i 1000`

* Generate a histogram of UDP RTTs and plot it using Gnuplot. Run with realtime priority 80 on CPU 1.

	Server: `./cyclicping -s -u udp -p 80 -a 1`

	Client: `./cyclicping -c -u udp:<serverip> -p 80 -a 1 -i 1000 -l 10000 -H 500 -g -q | gnuplot -p`

	This will for example produce the following plot:

	![Cyclicping histogram plot using Gnuplot](example-plot.png?raw=true)

## Acknowledgments

This work has been funded by the [fast realtime](https://de.fast-zwanzig20.de/basisvorhaben/fast-realtime/) project.

[![BMBF](https://de.fast-zwanzig20.de/wp-content/uploads/2016/02/BMBF_bunt.png)](https://www.bmbf.de/en/index.html) [![Zwanzig20](https://de.fast-zwanzig20.de/wp-content/uploads/2016/02/zwanzig20-1.png)](https://www.unternehmen-region.de/de/7661.php)
