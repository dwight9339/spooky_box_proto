"""PlatformIO task integration for the Spooky Box dual-core firmware.

This project is generated as a dual-core STM32CubeMX CMake project. PlatformIO
provides the VS Code task UI and OpenOCD package, while CMake remains the only
firmware build system.
"""

Import("env")

import os
from pathlib import Path
import shutil
import subprocess


PROJECT_DIR = Path(env.subst("$PROJECT_DIR")).resolve()
PACKAGES_DIR = Path(env.subst("$PROJECT_PACKAGES_DIR")).resolve()
PRESET = env.GetProjectOption("custom_cmake_preset", "Debug")
ADAPTER_SPEED = env.GetProjectOption("custom_adapter_speed", "1000")

CM7_ELF = PROJECT_DIR / "CM7" / "build" / "full_spooky_proto_CM7.elf"
CM4_ELF = PROJECT_DIR / "CM4" / "build" / "full_spooky_proto_CM4.elf"


def _latest_bundle_tool(bundle_name, executable):
    local_app_data = os.environ.get("LOCALAPPDATA")
    if not local_app_data:
        return None

    bundle_root = Path(local_app_data) / "stm32cube" / "bundles" / bundle_name
    matches = sorted(bundle_root.glob("*/bin/{}".format(executable)), reverse=True)
    return matches[0] if matches else None


def _find_tool(command, bundle_name, executable):
    found = shutil.which(command)
    if found:
        return Path(found)

    found = _latest_bundle_tool(bundle_name, executable)
    if found:
        return found

    raise RuntimeError(
        "Could not find {} on PATH or in the STM32Cube bundle directory".format(
            executable
        )
    )


def _build_environment():
    cmake = _find_tool("cmake", "cmake", "cmake.exe")
    ninja = _find_tool("ninja", "ninja", "ninja.exe")
    arm_gcc = _find_tool(
        "arm-none-eabi-gcc",
        "gnu-tools-for-stm32",
        "arm-none-eabi-gcc.exe",
    )

    process_env = os.environ.copy()
    tool_dirs = [str(cmake.parent), str(ninja.parent), str(arm_gcc.parent)]
    process_env["PATH"] = os.pathsep.join(tool_dirs + [process_env.get("PATH", "")])
    return cmake, process_env


def _run(command, process_env=None):
    print("\n> " + subprocess.list2cmdline([str(item) for item in command]))
    result = subprocess.run(
        [str(item) for item in command],
        cwd=str(PROJECT_DIR),
        env=process_env,
        check=False,
    )
    if result.returncode:
        raise RuntimeError("Command failed with exit code {}".format(result.returncode))


def _build_firmware():
    cmake, process_env = _build_environment()
    _run([cmake, "--preset", PRESET], process_env)
    _run([cmake, "--build", "--preset", PRESET], process_env)

    missing = [str(path) for path in (CM7_ELF, CM4_ELF) if not path.is_file()]
    if missing:
        raise RuntimeError("CMake completed but did not produce: " + ", ".join(missing))


def _openocd_paths():
    executable_name = "openocd.exe" if os.name == "nt" else "openocd"
    openocd_root = PACKAGES_DIR / "tool-openocd"
    executable = openocd_root / "bin" / executable_name
    script_candidates = (
        # Current PlatformIO/xPack Windows package layout.
        openocd_root / "openocd" / "scripts",
        # Conventional upstream and older PlatformIO package layout.
        openocd_root / "share" / "openocd" / "scripts",
        openocd_root / "scripts",
    )
    scripts = next((path for path in script_candidates if path.is_dir()), None)

    if not executable.is_file():
        raise RuntimeError(
            "PlatformIO tool-openocd is missing. Reopen the project or run "
            "'pio pkg install' so platform_packages can be installed."
        )
    if scripts is None:
        raise RuntimeError(
            "Could not find the OpenOCD scripts directory under {}".format(
                openocd_root
            )
        )
    return executable, scripts


def _tcl_path(path):
    # Tcl braces keep paths containing spaces together; forward slashes also
    # avoid Windows backslash escaping inside an OpenOCD command string.
    return "{{{}}}".format(path.resolve().as_posix())


def _openocd_base_command(dual_core=False):
    executable, scripts = _openocd_paths()
    command = [
        executable,
        "-s",
        scripts,
        "-f",
        "interface/cmsis-dap.cfg",
        "-c",
        "transport select swd",
        "-c",
        # Programming both flash banks only requires the CM7/AP0 target. Avoid
        # making OpenOCD's reset-halt sequence wait for a sleeping or boot-held
        # CM4. The probe target still enables both cores for discovery.
        "set DUAL_CORE {}".format(1 if dual_core else 0),
        "-c",
        "set DUAL_BANK 1",
        "-f",
        "target/stm32h7x.cfg",
        "-c",
        # AP2 supplies optional trace/low-power debug configuration, but it can
        # be unavailable while the H755 D2 domain is asleep. Flash access uses
        # CM7/AP0, so do not examine AP2 or run the AP2-based examine hook.
        "stm32h7x.ap2 configure -defer-examine",
        "-c",
        "stm32h7x.cpu0 configure -event examine-end {}",
    ]
    if dual_core:
        command += [
            "-c",
            "stm32h7x.cpu1 configure -event examine-end {}",
        ]
    command += [
        "-c",
        # Keep stm32h7x.cfg's default reset behavior. This matches the first
        # successful Pico deployment and does not require the NRST wire merely
        # to establish an SWD connection.
        "adapter speed {}".format(ADAPTER_SPEED),
    ]
    return command


def build_firmware_action(target, source, env):
    del target, source, env
    _build_firmware()


def deploy_action(target, source, env):
    del target, source, env
    _build_firmware()
    command = _openocd_base_command() + [
        "-c",
        "program {} verify".format(_tcl_path(CM7_ELF)),
        "-c",
        "program {} verify reset exit".format(_tcl_path(CM4_ELF)),
    ]
    _run(command)


def deploy_cm7_action(target, source, env):
    del target, source, env
    _build_firmware()
    command = _openocd_base_command() + [
        "-c",
        "program {} verify reset exit".format(_tcl_path(CM7_ELF)),
    ]
    _run(command)


def probe_action(target, source, env):
    del target, source, env
    command = _openocd_base_command(dual_core=True) + [
        "-c",
        "init; targets; shutdown",
    ]
    _run(command)


env.AddCustomTarget(
    name="firmware",
    dependencies=None,
    actions=build_firmware_action,
    title="Build Firmware",
    description="Build the CM7 and CM4 ELF files with the existing CMake preset",
)

env.AddCustomTarget(
    name="deploy",
    dependencies=None,
    actions=deploy_action,
    title="Deploy",
    description="Build, flash, verify, reset, and run both H755 cores via Picoprobe",
)

env.AddCustomTarget(
    name="deploy_cm7",
    dependencies=None,
    actions=deploy_cm7_action,
    title="Deploy CM7",
    description="Build both cores but update only the CM7 flash bank via Picoprobe",
)

env.AddCustomTarget(
    name="probe",
    dependencies=None,
    actions=probe_action,
    title="Probe Test",
    description="Connect to the H755 and list its OpenOCD targets without flashing",
)
