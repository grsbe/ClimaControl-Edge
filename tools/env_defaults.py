# PlatformIO pre-build script: reads the factory passwords from .env (git-ignored) and passes them to
# the compiler as CC_DEFAULT_AP_PASSWORD / CC_DEFAULT_WEB_PASSWORD (used in lib/core/src/Config.h).
# Environment variables with the same names take precedence (useful for CI).
# Setup: copy .env.example to .env and choose the passwords.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

REQUIRED = {
    "CC_DEFAULT_AP_PASSWORD": "password of the controller's own WiFi access point (8-63 characters)",
    "CC_DEFAULT_WEB_PASSWORD": "password for changes in the web UI / HTTP API",
}


def read_dotenv(path):
    values = {}
    if not os.path.exists(path):
        return values
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip().strip('"').strip("'")
    return values


dotenv_path = os.path.join(env.subst("$PROJECT_DIR"), ".env")  # noqa: F821
values = read_dotenv(dotenv_path)
values.update({k: os.environ[k] for k in REQUIRED if os.environ.get(k)})

problems = [f"  {k}: {desc}" for k, desc in REQUIRED.items() if not values.get(k)]
ap = values.get("CC_DEFAULT_AP_PASSWORD", "")
if ap and not 8 <= len(ap) <= 63:
    problems.append("  CC_DEFAULT_AP_PASSWORD must be 8-63 characters (WPA2 rule)")
if problems:
    print("\n*** Factory passwords missing or invalid. Copy .env.example to .env and fill in:")
    print("\n".join(problems) + "\n")
    env.Exit(1)  # noqa: F821

for key in REQUIRED:
    env.Append(CPPDEFINES=[(key, env.StringifyMacro(values[key]))])  # noqa: F821
