# libSceSysmodule technical debt

### Functional

- [sceSysmoduleGetModuleInfoForUnwind](../../../core/libs/prx/libSceSysmodule/Export.cpp) - forwards to the kernel query, preserving its exception tables and error results, as [shadPS4](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/sysmodule/sysmodule.cpp) does. Console equivalence is unmeasured. Kernel limitations for host images, relinked guest images and platform unwind metadata remain.
