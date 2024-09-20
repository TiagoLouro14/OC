1 Introduction
Multilevel cache hierarchies are common in today computers with the first levels of caches integrated in the processor chip.
The goal of this assignment is the development of a memory hierarchy composed of up to two levels of caches. To achieve this, students will complete the base code provided by the faculty to implement L1 (first level) and L2 (second level) caches from scratch. Then students must integrate these components to form a complete cache hierarchy.
2 Development Environment
The simulator is to be developed in C and target x86-64 Linux. The only dependencies allowed are those provided by glibc (stdio.h, stdlib.h ...).
The simulator must be built with make or make all without any warnings. For testing purposes, it must also run on the lab computers (in Linux).
Students are free to modify the Makefile and add more build targets during the development of the simulator, but the CFLAGS must remain the same.
