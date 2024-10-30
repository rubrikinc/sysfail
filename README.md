Sysfail is a shared library that helps test applications with failure-injection.

## Overview

Automated testing is key to building high quality software. Most modern
software depends on external services or processes such as databases, queues,
caches etc in addition to other stateful components like filesystems and
devices.
System-calls are the common interface for userland applications use to talk to
other processes / services and to the operating-system itself which makes them
a powerful abstraction for testing applications' failure-resilience and
robustness.
Sysfail helps declaratively expose application (system under test) to failures
and delays and helps harden non-functional properties such as idempotency,
checkpoint-recovery, retries / ability to make progress in the face
of degradation / errors etc.

## Features

* Inject failures and / or delay to any system-call of choice
* Specify mix / weights for errors that are presented in response to failure
* Fine grained control on threads that are failure-injected
* Specify fraction of errors that are injected before or after the syscall
* Modern C++23 interface for C++ based applications
* C API that also serves as foreign-function-interface (FFI) for other languages (eg. Golang)
* Ability to failure-inject regardless of extent of control on the call-site (eg. 3rd-party libraries)

## Limitations

* At the moment Sysfail only supports Linux + x86_64 / amd64 platform. Patches are welcome!
* While ABI for use over FFI-bridge exists, it does not have idomatic wrappers for other languages yet
* It makes use of syscall-user-dispatch (aka SUD) (https://github.com/torvalds/linux/blob/master/Documentation/admin-guide/syscall-user-dispatch.rst), so requires Linux version 5.11 or higher.
* `libc` sometimes quiesces all signal-handlers (which SUD uses) for brief
  periods (eg. while creating new threads). Sysfail disables failure injection
  in such cases to avoid breaking `libc`'s assumptions
* Some syscalls such as `SYS_rt_sigprocmask` are never failure-injected because
  this would break `libc` code-paths such as the one described above.

## Install

Sysfail needs g++, libtbb-dev and libgtest-dev.

```
$ git clone https://github.com/rubrikinc/sysfail
$ cd sysfail
$ mkdir build
$ cd build
$ cmake ..
$ make
$ make install
```

OR

```
$ git clone https://github.com/rubrikinc/sysfail
$ docker build -t sysfail:test .
```
which creates a local ubuntu image with sysfail installed. Feel free to modify
Dockerfile to your liking (eg. preferred base-image, install location etc).

Once built, one can play with sysfail the container
```
$ docker run -it sysfail:test /bin/bash
# make
...
```
or build and test with it, like so
```
...
# su - ubuntu
$ g++ -o flaky_print flaky_print.cc -lsysfail
```
(`flaky_print.cc` should already be present)


## Using sysfail

The headers `sysfail.hh` and `sysfail.h` contain fully documented API for C++
and C respectively. Linker flag `-lsysfail` links with the library.

Test files `session_test.cc` and `cwrapper_test.cc` serve as working examples of
usage in C++ and C. `ffi.go` uses FFI (foreign-function-interface) in a
standalone-process form-factor, so can serve as an example for non C / C++
projects.
