# Tutorial 1: Understanding ucontext_t

## Dependencies

This repository uses CMake to configure and build each binary.
The `teach.cs` machines have version 3.16 already installed.
But if you are working locally (not recommended), you will need to install CMake as well.

## Compiling

CMake can configure the project for different build systems and IDEs (type `cmake --help` for a list of generators available for your platform).

You can also work via the command line.
We recommend you create a build directory before invoking CMake to configure the project (`cmake -B`).
For example, we can perform the configuration step from the project root directory:

	cmake -H. -Bcmake-build-release -DCMAKE_BUILD_TYPE=Release
	cmake -H. -Bcmake-build-debug -DCMAKE_BUILD_TYPE=Debug

After the configuration step, you can ask CMake to build the project.

	cmake --build cmake-build-release/ --target all
	cmake --build cmake-build-debug/ --target all

## Running

If compilation was successful, you will find the compiled binaries **inside your build directory** (e.g., `cmake-build-debug`).
For example, assuming your current working directory is `cmake-build-debug`, then:

```console
wolf:~/testme/cmake-build-debug$ ./src/csc369_t01
main: setcontext_called = 0
start = 0x5576d87af000
end = 0x5576d87b0000
main: setcontext_called = 1
```
