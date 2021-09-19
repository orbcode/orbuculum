# Status
* `orbcat` - works
* `orbtop` - works
* `orbuculum` - builds, serial feeder works, others - untested

# Windows-related changes
* Add `WSAStartup` at startup
* Use `send` and `recv` to handle sockets (instead of `read` and `write`)
* `nwclient.c` - create pipe with `CreatePipe`
* `bcopy` -> `memcpy` and `bzero` -> `memset`. MinGW GCC reports warnings about `bcopy` and `bzero` as they are not in C standard
* Few POSIX<->MinGW mismatches, nothing scary (e.g. `st_mtime` vs `st_mtim`)
* serial feeder for `orbuculum` written from scratch using WinAPI functions.
    * `ReadFile` call waits until buffer is filled or timeout is reached.
    * Relaying on timeout will result in data flowing in batches, not as soon as arrived.
    * Loop works as follows:
        * wait for event 'data pending'
        * read number of bytes waiting to be received
        * read that amount of bytes using `ReadFile`
    * I'm not 100% sure that this loop will work in all cases, works in my setup (2MHz SWO) and reads ~500 bytes each time