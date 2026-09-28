# STM32CubeMX scratch generation and diff

Use this procedure only to evaluate a CubeMX export. It never authorizes generation into
the working repository. The current freeze and known mismatches are in the
[CubeMX reconciliation register](../design/cubemx-reconciliation.md).

## Preconditions

- No recording or hardware connection is required.
- Coordinate with other agents and start from a clean, preserved working tree. Do not
  run this procedure while unrelated generated-shell changes are uncommitted.
- Record `git rev-parse HEAD`, the CubeMX version, its device-database version and the
  STM32CubeH7 package version before comparing output.
- Use a new scratch directory. Never point CubeMX at the repository root.

For the currently recorded toolchain on Windows, CubeMX is installed under
`C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX`, the `.ioc` records
CubeMX 6.17.0, and the project records STM32CubeH7 1.13.0.

## Generate in isolation

Create a command file outside the source tree with one command per line. Replace the two
absolute paths; keep the output path outside the repository or under ignored `build/`.

```text
config load "C:\path\to\full_spooky_proto\full_spooky_proto.ioc"
project path "C:\scratch\spooky-cubemx"
project name full_spooky_proto
project generate
exit
```

Run quiet script mode from the CubeMX installation directory:

```powershell
$spookyCubeMx = 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX'
& "$spookyCubeMx\jre\bin\java.exe" -jar "$spookyCubeMx\STM32CubeMX.exe" `
  -q 'C:\scratch\spooky-cubemx-script.txt'
```

The launcher executable by itself may exit without executing the script on Windows;
invoke the bundled Java runtime as shown. Do not use `generate code <path>` for this
dual-core project: it does not construct the full project layout.

Fail the run if CubeMX reports `KO`, throws an exception, opens a prompt, hangs, modifies
the source checkout, or does not produce both CM7 and CM4 outputs. CubeMX 6.17 currently
fails at `project generate` with an internal `IocParser` null exception, so the current
expected verdict is **fail; generation freeze remains active**.

## Review the complete generated surface

If generation succeeds, compare rather than copy. Review these paths individually so a
missing hand-maintained directory cannot be mistaken for a generator deletion:

```text
CM4/Core/
CM7/Core/
CM7/USB_Device/
CM4/mx-generated.cmake
CM7/mx-generated.cmake
mx-generated.cmake
```

Use `git diff --no-index -- <maintained-path> <scratch-path>` for every directory or
file and retain the complete output for review. Also compare the two linker scripts and
the generated `.ioc`. Treat root and per-core `CMakeLists.txt` and the toolchain file as
maintained files even if CubeMX emits replacements.

Classify every hunk before applying anything:

- expected representation of a reconciled `.ioc` setting;
- required restoration of a registered runtime override;
- harmless generator/version churn;
- unexplained behavioral or ownership change.

An unexplained hunk fails the gate. Apply accepted changes selectively with normal source
review; never recursively copy the scratch output over the repository.

## Validate an accepted candidate

After selective changes, run all four canonical paired-image builds:

```text
cmake --preset Debug
cmake --build --preset Debug
cmake --preset Release
cmake --build --preset Release
cmake --preset IpcSmoke
cmake --build --preset IpcSmoke
cmake --preset IpcMismatch
cmake --build --preset IpcMismatch
```

Then perform every applicable bench check listed in the reconciliation register. Restore
the last qualified image pair after an experimental mismatch test. Preserve dated bench
evidence, including failed or partial results, and do not call the `.ioc` authoritative
until the entire acceptance gate passes.
