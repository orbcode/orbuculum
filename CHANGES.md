(Version 1.00-InProgress)

* Change to BSD from GPL License
* Use of nextpnr-ice40 instead of arachne-pnr
* Extensive changes to gdbtrace.init 

5th August 2019 (Version 0.22)

* Internal simplifications and tidying
* Fix signal bug which could lead to orbuculum bailing when a client disconnected

4th August 2019 (Version 0.21)

* gdbinit is changed to reference SWO rather than SWD (Issue #22)
* Support for higher speed connections using linux kernels that support the BOTHER option.
* Addition of C++ demangling support.
* Specific gdbinit support for STM32F4
* Small changes to support libftdi1.4 exclusively. It's essential you use this version of libftdi or higher!

