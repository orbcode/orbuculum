V1.10 Under construction

* Addition of JSON output for orbtop
* povray splash screen generator

9th September 2019 (Version 1.00)

* Change to BSD from GPL License
* Fixes to command line options, raw output and HW event decoding
* Use of nextpnr-ice40 instead of arachne-pnr
* Extensive changes to gdbtrace.init 

14th August 2019 (Version 0.23)

* Small edits to gdbtrace.init for tidying purposes
* Fix integration time units in orbtop
* Allow orbtop to gracefully restart when elf file is changed (e.g. on recompile)

13th August 2019 (Version 0.22 - no functional changes)

* Makefile/Include path change to allow compilation on OpenSuse

5th August 2019 (Version 0.22)

* Internal simplifications and tidying
* Fix signal bug which could lead to orbuculum bailing when a client disconnected

4th August 2019 (Version 0.21)

* gdbinit is changed to reference SWO rather than SWD (Issue #22)
* Support for higher speed connections using linux kernels that support the BOTHER option.
* Addition of C++ demangling support.
* Specific gdbinit support for STM32F4
* Small changes to support libftdi1.4 exclusively. It's essential you use this version of libftdi or higher!

