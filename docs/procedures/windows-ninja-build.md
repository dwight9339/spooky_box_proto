# Windows Ninja firmware builds

The canonical firmware build uses the root CMake presets and produces a matched
CM7/CM4 image pair:

```text
cmake --preset Debug
cmake --build --preset Debug
```

Use the same configure-then-build sequence for `Release`, `IpcSmoke`, and
`IpcMismatch`. CMake, Ninja, and the ARM GNU toolchain must be available on
`PATH`. Configure immediately before building when a build tree may have been
created by an IDE or another shell; this refreshes the absolute CMake command
recorded in the generated Ninja files.

## Restricted-process sandbox limitation

On Windows, Ninja 1.13.2 can wait indefinitely before starting its first command
when it is run inside a restricted process sandbox. The observed signature is:

- configuration succeeds;
- `cmake --build --preset <Preset> --verbose` prints `Run Build Command(s)` and
  then no build edge;
- `ninja -n` and `ninja -t targets` complete;
- a minimal Ninja graph also waits when its command names any valid executable;
  and
- the same minimal graph and canonical preset build complete outside the
  sandbox.

This signature is a process-launch incompatibility at the Ninja/sandbox
boundary, not a firmware compile failure or a deadlock between the CM4 and CM7
child builds. Reducing parallelism with `-j1` does not address it.

When the signature matches, run the documented preset commands in a normal
Windows terminal or grant the build command permission to execute outside the
process sandbox. Do not replace the root build with separately assembled core
images: the root preset remains responsible for applying matching options to
both cores. NMake may be useful for diagnosis, but it is not the canonical
generator and is not required as a repository workaround.

After the build completes, verify that both files exist beneath the same preset:

```text
build/<Preset>/firmware/CM7/full_spooky_proto_CM7.elf
build/<Preset>/firmware/CM4/full_spooky_proto_CM4.elf
```
