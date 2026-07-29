Import("env")

import subprocess
from datetime import datetime, timezone

# Injects the running firmware's real git identity as a build flag --
# FIRMWARE_VERSION is exposed over serial and /status so a device can be
# checked (or a mystery board on the network can be ruled in/out) without
# trusting a hand-maintained version number that could drift out of sync
# with what's actually flashed. A plain Python script (SCons env.Append),
# not `!` shell substitution in platformio.ini -- that syntax's quoting
# isn't portable between the Bash/PowerShell shells this project gets
# built from.
#
# Branch name + commit/build timestamps, not git describe's own --dirty
# flag: "dirty" only flags uncommitted changes, but the far more common
# state during development is a fully-committed feature/topic branch that
# simply hasn't been merged to main yet -- the branch name says that
# directly, so it's shown instead of trying to infer it from a bare hash.
project_dir = env["PROJECT_DIR"]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=project_dir, stderr=subprocess.DEVNULL).decode().strip()


try:
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    short_hash = git("rev-parse", "--short", "HEAD")
    commit_time = git("log", "-1", "--format=%cI")
    uncommitted = "+uncommitted" if git("status", "--porcelain") else ""
    build_time = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    # No spaces -- BUILD_FLAGS values get passed to the compiler as a
    # plain space-delimited command line, so a space here silently
    # word-splits the define into multiple bogus arguments (confirmed
    # live: "linker input file not found" errors from the fragments).
    version = f"{branch}@{short_hash}{uncommitted},commit={commit_time},built={build_time}"
except (subprocess.CalledProcessError, FileNotFoundError):
    version = "unknown"

env.Append(BUILD_FLAGS=['-DFIRMWARE_VERSION=\\"' + version + '\\"'])
