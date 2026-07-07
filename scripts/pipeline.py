#!/usr/bin/env python3
"""Build -> flash (auto port) -> 30s monitor with log.

Linux-only. Discovers ESP-IDF environment automatically.

Usage:
    python scripts/pipeline.py
"""

import subprocess
import sys
import time
import os
import re
import shutil
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent

sys.path.insert(0, str(Path(__file__).parent))
from monitor import monitor_port
from find_port import find_esp32_port


def find_idf_py() -> str:
    """Locate idf.py: check PATH first (user sourced export.sh), then ~/.espressif."""
    exe = shutil.which("idf.py")
    if exe:
        return exe

    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        esp_home = Path.home() / ".espressif"
        candidates = sorted(esp_home.glob("v*/esp-idf/tools/idf.py"))
        if candidates:
            idf_path = str(candidates[-1].parent.parent)
        else:
            for p in ["/opt/esp-idf", "/usr/local/opt/esp-idf"]:
                if Path(p).exists():
                    idf_path = p
                    break
    if not idf_path:
        print("ERROR: ESP-IDF not found. Source ESP-IDF environment:", flush=True)
        print("  . /path/to/esp-idf/export.sh", flush=True)
        sys.exit(1)

    idf_py = str(Path(idf_path) / "tools" / "idf.py")
    if not os.path.exists(idf_py):
        print(f"ERROR: {idf_py} not found", flush=True)
        sys.exit(1)
    return idf_py


def build_idf_env(idf_py: str) -> dict:
    """Build a full IDF environment by sourcing export.sh via activate.py.

    Returns env dict with toolchain, python venv, idf.py all in PATH.
    """
    idf_path = str(Path(idf_py).parent.parent)
    env = os.environ.copy()

    # Pre-set required vars for activate.py
    env["IDF_PATH"] = idf_path
    env["IDF_TOOLS_PATH"] = str(Path.home() / ".espressif")

    # Find and set IDF_PYTHON_ENV_PATH
    idf_python_venv = os.environ.get("IDF_PYTHON_ENV_PATH")
    if not idf_python_venv:
        python_envs = sorted(
            (Path.home() / ".espressif").glob("python_env/idf*_py*/bin/python")
        )
        if python_envs:
            idf_python_venv = str(python_envs[-1].parent.parent)
    if idf_python_venv:
        env["IDF_PYTHON_ENV_PATH"] = idf_python_venv

    env["ESP_PYTHON"] = os.path.join(idf_python_venv, "bin", "python")
    env["IDF_PYTHON_CHECK_CONSTRAINTS"] = "no"

    # Run activate.py to get the env file
    activate_py = os.path.join(idf_path, "tools", "activate.py")
    try:
        result = subprocess.run(
            [env["ESP_PYTHON"], activate_py, "--export", "--shell", "bash"],
            capture_output=True, text=True, env=env, timeout=30,
        )
    except subprocess.TimeoutExpired:
        print("ERROR: activate.py timed out", flush=True)
        sys.exit(1)

    if result.returncode != 0:
        print(f"ERROR: activate.py failed:\n{result.stderr}", flush=True)
        sys.exit(1)

    # Parse the source command from stdout
    match = re.search(r'^\. (\S+)', result.stdout, re.MULTILINE)
    if not match:
        print(f"ERROR: could not parse activate.py output:\n{result.stdout}", flush=True)
        sys.exit(1)

    activate_file = match.group(1)
    if not os.path.exists(activate_file):
        print(f"ERROR: activate file {activate_file} not found", flush=True)
        sys.exit(1)

    # Parse the activate file for export statements,
    # expanding $VAR / ${VAR} references using the current env.
    def _expand(val: str, env: dict) -> str:
        def _repl(m):
            name = m.group(1)
            return env.get(name, "")
        val = re.sub(r'\$\{(\w+)\}', _repl, val)
        val = re.sub(r'\$(\w+)', _repl, val)
        return val

    content = Path(activate_file).read_text()
    for line in content.splitlines():
        line = line.strip()
        if line.startswith("export "):
            rest = line[7:]
            eq = rest.find("=")
            if eq == -1:
                continue
            key = rest[:eq]
            val = rest[eq + 1:]
            if len(val) >= 2 and val[0] == val[-1] and val[0] in ('"', "'"):
                val = val[1:-1]
            val = _expand(val, env)
            env[key] = val

    # Remove duplicate/consecutive colons in PATH (from unexpanded $PATH)
    env["PATH"] = re.sub(r':+', ":", env.get("PATH", ""))
    # Remove trailing colon
    env["PATH"] = env["PATH"].rstrip(":")

    return env


def idf_run(args, idf_py, env):
    """Run idf.py with the IDF venv's Python and return completed process."""
    idf_python = os.path.join(env["IDF_PYTHON_ENV_PATH"], "bin", "python")
    cmd = [idf_python, idf_py] + args
    return subprocess.run(cmd, env=env)


def log(msg):
    print(f"  {msg}", flush=True)


def main():
    idf_py = find_idf_py()
    env = build_idf_env(idf_py)

    os.chdir(PROJECT_DIR)

    log("Build...")
    r = idf_run(["build"], idf_py, env)
    if r.returncode:
        sys.exit(1)

    port = find_esp32_port()
    if not port:
        log("No ESP32 port found")
        sys.exit(1)
    log(f"Port: {port}")

    log("Flash...")
    r = idf_run(["-p", port, "flash"], idf_py, env)
    if r.returncode:
        sys.exit(1)

    time.sleep(0.3)
    sys.exit(monitor_port(port, timeout=30, log_dir=PROJECT_DIR / "logs", no_reset=True))


if __name__ == "__main__":
    main()
