Import("env")

import subprocess


def resolve_version() -> str:
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
        return version or "dev"
    except Exception:
        return "dev"


version = resolve_version().replace('"', "")
env.Append(CPPDEFINES=[("FW_VERSION", '\\"{}\\"'.format(version))])
print("[version] FW_VERSION={}".format(version))
