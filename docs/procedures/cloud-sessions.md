# Cloud sessions

Claude Code cloud sessions run in a fresh container with this repository cloned. This
procedure covers the cloud environment they need and how their Beads writes reach the
canonical Beads database. Hardware work stays on the dev machine: a cloud session
builds and runs host tests, but it cannot flash or run bench tests.

## Cloud environment

Configure the environment once in its settings (the cloud environment menu in a
session's title bar, then Edit). The setup script's result is cached for about seven
days and rebuilt when the script or the allowed hosts change, so sessions start with
the tools in place. See [cloud environments](https://code.claude.com/docs/en/cloud-environments).

- **Network access:** Trusted. It covers the Ubuntu archive, npm and GitHub, which
  the setup script uses. Arm's own toolchain download host is not on that list, so
  the script installs the same toolchain release from Ubuntu.
- **Environment variables:** `USERPROFILE=/opt`. `CM7/CMakeLists.txt` looks for
  STM32CubeH7 under `$USERPROFILE/STM32Cube/Repository/`, as on Windows.
- **Setup script:**

```bash
#!/bin/bash
# Spooky Box cloud environment: firmware toolchain, FatFs and the Beads CLI.
# A failed step is reported, not fatal: a non-zero exit would stop the session
# from starting at all.
export DEBIAN_FRONTEND=noninteractive

# ARM GNU toolchain 13.2.rel1 (arm-none-eabi-gcc 13.2.1) with newlib-nano.
(apt-get update -qq && apt-get install -y -qq gcc-arm-none-eabi libnewlib-arm-none-eabi \
  || echo "setup: ARM toolchain install failed" >&2) &

# Beads CLI; keep this version in step with the dev machine's bd.
(npm install -g --silent @beads/bd@1.3.1 || echo "setup: bd install failed" >&2) &

# FatFs R0.12c from STM32CubeH7 V1.13.0 (submodule commit fc7f8ed), the only
# part of the Cube the build reads. CM7/CMakeLists.txt looks for it under
# $USERPROFILE, so the environment sets USERPROFILE=/opt.
fatfs=/opt/STM32Cube/Repository/STM32Cube_FW_H7_V1.13.0/Middlewares/Third_Party/FatFs
(mkdir -p "$fatfs" && cd "$fatfs" && git init -q \
  && git fetch -q --depth 1 https://github.com/STMicroelectronics/stm32-mw-fatfs.git \
       fc7f8edc3ac2e263c2f231026053e15d3587b34b \
  && git checkout -q FETCH_HEAD || echo "setup: FatFs fetch failed" >&2) &

wait
arm-none-eabi-gcc --version | head -1
bd version
ls "$fatfs/src/ff.c"
exit 0
```

CMake, Ninja, GCC and Python come with the cloud image. Spooky Bench's Python tests
need `spookyprobe`, which is not installed in the cloud; run them on the dev machine.

Each session's own Beads copy comes from the repository's SessionStart hook
(`.claude/hooks/session-start.sh`): in a cloud session with no database yet it runs
`bd bootstrap` from the Beads remote, then `bd prime` as in every session.

## Beads writes from a cloud session

A cloud session can read the Beads remote but cannot push to it. Its writes travel
in an outbox file committed with its branch, and the dev machine replays them.

**In the cloud session.** Run every Beads write through
[`host/tools/bd_outbox.py`](../../host/tools/bd_outbox.py):

```text
python3 host/tools/bd_outbox.py bd update <id> --append-notes "..."
```

The tool runs `bd` on the session's copy and appends the command to
`.beads/outbox/<branch>.jsonl`; commit that file with the branch. Reads (`bd show`,
`bd list`, `bd ready`, ...) run as given and are not recorded, so they can call `bd`
directly. The tool refuses `bd dolt`, `bd sync`, `bd import`, `bd bootstrap` and
other commands that sync or rebuild the database, and `bd create --graph`. A new
issue is created one `bd create` or `bd q` at a time; the tool prints its ID.

Do not edit an outbox by hand once it is pushed. To undo a write, record the
opposite write.

**On the dev machine.** After pulling the branch (or merging it):

```text
python host/tools/bd_outbox.py pending
python host/tools/bd_outbox.py apply --dry-run
python host/tools/bd_outbox.py apply
bd dolt push
```

`apply` replays each outbox in order, oldest first, as the session's actor
(`--actor`). An issue the session created gets whatever ID the canonical database
assigns; later commands in the same outbox that name the session's ID, in full
(`full_spooky_proto-a1b`) or short (`a1b`), are rewritten to the new one, and
`apply` prints each mapping. Progress is kept in `<outbox>.applied.json` beside the
outbox (ignored by Git), so a rerun skips what was applied and resumes after a
failure. When a command fails, `apply` stops and names it: fix the cause (for
example, an issue that was closed locally in the meantime) and run `apply` again.

Once the push succeeds, delete the applied outbox file and its `.applied.json`, and
commit the deletion on the branch that carries the outbox (or on the branch it was
merged into).

Other text that quotes a session-created ID, such as a commit message or a
document, is not rewritten; `apply` prints the mapping so it can be corrected.
