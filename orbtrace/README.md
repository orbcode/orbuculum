ORBTrace Development
====================

This is the development status for the ORBTrace parallel TRACE hardware. Its very unlikely you want to be here but, just in case you do, this is built using Clifford Wolfs' icestorm toolchain and currently targets a either a lattice iCE40HX-8K board or the lattice icestick.

It is very much work in progress. It is currently functional for 1 and 2 bit trace widths.

To build it perform;

```
cd src
make ICE40HX8K_B_EVN

```
or;

```
cd src
make ICE40HX1K_STICK_EVN

```

Information on how to integrate it with orbuculum (hint, the `-o` option) is in te main README.
