# W02 Lecture Demonstrations

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
This section assumes you are running on the teach.cs machines and that you are in your build directory.

Before running the binaries, you will want to disable address space randomization:

    setarch `uname -m -R $SHELL`

### Example: Direct execution on a physical CPU

1. Read the source:

        less ../src/cpu.c

2. Run the binary on a CPU:

        numactl --physcpubind 1 ./src/cpu hi

3. Run two of the same binary on the same CPU:

        numactl --physcpubind 1 ./src/cpu hi & numactl --physcpubind 1 ./src/cpu there

Identify that both processes get a roughly equal amount of execution time on the same physical CPU, and don't have to be aware (i.e., do anything in the source) of any other processes.

### Example: Virtual address spaces

Using the `mem` binary,

1. Show the source:

        less ../src/mem.c

2. Run the binary:

        ./src/mem 10

    Explain the output: Process ID (e.g., 23364), address of the pointer somewhere on the stack, incrementing variable

3. Run two copies of the binary:

        ./src/mem 10 & ./src/mem 200

Identify the two different process IDs.
Identify that the address of the variable `p` is identical to both programs.
But the value of p for each process ID is different.
