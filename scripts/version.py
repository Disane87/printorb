# PlatformIO pre-build hook: inject -DPRINTORB_VERSION.
# Priority: env PRINTORB_VERSION (CI) -> git tag -> "0.0.0-dev".
Import("env")
import os, subprocess

def resolve_version():
    v = os.environ.get("PRINTORB_VERSION", "").strip()
    if v:
        return v
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--abbrev=0"],
            stderr=subprocess.DEVNULL).decode().strip()
        if v[:1] in ("v", "V"):
            v = v[1:]
        if v:
            return v
    except Exception:
        pass
    return "0.0.0-dev"

version = resolve_version()
env.Append(CPPDEFINES=[("PRINTORB_VERSION", env.StringifyMacro(version))])
print("PrintOrb firmware version: %s" % version)
