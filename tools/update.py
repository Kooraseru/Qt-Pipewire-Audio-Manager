#!/usr/bin/env python3
"""Build/test this checkout and deploy an independent user-session backend."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import zipfile
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve().parent.parent
SERVICE = "plasma-audio-manager.service"
BUS = "org.plasmaaudiomanager.Prototype1"
OBJECT = "/org/plasmaaudiomanager/Prototype1"
WIDGET = "org.kooraseru.plasma.audiomanager"


def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def atomic_write(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".pam-", delete=False) as file:
            temporary = Path(file.name)
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
            os.fchmod(file.fileno(), mode)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def bus_call(service, path, interface, method, *arguments):
    result = run("busctl", "--user", "--json=short", "call", service, path,
                 interface, method, *arguments, capture_output=True, text=True, timeout=5)
    return json.loads(result.stdout)["data"][0]


def owner_pid():
    arguments = ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus")
    if not bus_call(*arguments, "NameHasOwner", "s", BUS):
        return None
    try:
        return bus_call(*arguments, "GetConnectionUnixProcessID", "s", BUS)
    except subprocess.CalledProcessError:
        # Handle ownership disappearing between calls without parsing localized errors.
        if not bus_call(*arguments, "NameHasOwner", "s", BUS):
            return None
        raise


def service_property(name):
    return run("systemctl", "--user", "show", SERVICE, "--property=" + name,
               "--value", capture_output=True, text=True).stdout.strip()


def owned_devices():
    return json.loads(bus_call(BUS, OBJECT, BUS, "OwnedDevices"))


def owned_routes():
    # Older deployed backends predate route management.
    xml = bus_call(BUS, OBJECT, "org.freedesktop.DBus.Introspectable", "Introspect")
    interface = ET.fromstring(xml).find(f"interface[@name='{BUS}']")
    if interface is None or interface.find("method[@name='Routes']") is None:
        return []
    return json.loads(bus_call(BUS, OBJECT, BUS, "Routes"))


def restart_blockers():
    devices = owned_devices()
    routes = owned_routes()
    if any(route.get("state") != "Failed" for route in routes):
        return "Custom routes are active. Stop the routed streams before updating; saved rules will be retained."
    if any(not device.get("persistent", False) for device in devices):
        return "Temporary devices are still owned by this backend; restarting would lose them."
    if devices:
        graph = json.loads(bus_call(BUS, OBJECT, BUS, "Snapshot"))["objects"]
        prefixes = [device["nodePrefix"] for device in devices]
        owned = {str(obj["id"]) for obj in graph if obj["type"].endswith(":Node") and
                 any(obj["properties"].get("node.name", "") in (prefix, prefix + ".sink", prefix + ".source") for prefix in prefixes)}
        if any(str(obj["id"]) in owned and obj.get("state") == "running" for obj in graph):
            return "A saved virtual device is running. Stop its audio clients before updating."
        if any(obj["type"].endswith(":Link") and
               (obj["properties"].get("link.output.node") in owned or obj["properties"].get("link.input.node") in owned) for obj in graph):
            return "A saved virtual device has audio connections. Disconnect its clients before updating."
    return ""


def widget_current(archive, installed):
    if not installed.is_dir():
        return False
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            if entry.is_dir():
                continue
            destination = installed / entry.filename
            if not destination.is_file() or destination.read_bytes() != package.read(entry):
                return False
    return True


def deploy(build, backend_only):
    home = Path.home()
    runtime = home / ".local/libexec/plasma-audio-manager"
    binary = runtime / "pam-backend"
    unit = Path(os.environ.get("XDG_CONFIG_HOME", home / ".config")) / "systemd/user" / SERVICE
    helper = home / ".local/bin/pam-update"
    data_home = Path(os.environ.get("XDG_DATA_HOME", home / ".local/share"))
    state_home = Path(os.environ.get("XDG_STATE_HOME", home / ".local/state")) / "plasma-audio-manager"
    new_binary = (build / "pam-backend").read_bytes()
    new_unit = (SOURCE / "systemd" / SERVICE).read_bytes()
    old_binary = binary.read_bytes() if binary.exists() else None
    old_unit = unit.read_bytes() if unit.exists() else None
    if old_unit is not None and not old_unit.startswith(b"# Managed by Plasma Audio Manager"):
        raise RuntimeError(f"Preserving the existing custom service file: {unit}")
    if (helper.exists() or helper.is_symlink()) and helper.resolve() != Path(__file__).resolve():
        raise RuntimeError(f"Preserving the existing command: {helper}")
    active = service_property("ActiveState") == "active"
    pid = owner_pid()
    managed_pid = int(service_property("MainPID") or "0") if active else 0
    replacing_manual = pid is not None and pid != managed_pid
    restart = not active or old_binary != new_binary or old_unit != new_unit
    if replacing_manual:
        executable = Path(os.readlink(f"/proc/{pid}/exe").removesuffix(" (deleted)")).resolve()
        if executable not in {binary.resolve(), (build / "pam-backend").resolve()}:
            raise RuntimeError(f"Another backend owns D-Bus (PID {pid}, {executable}); leaving it running")
        restart = True
    if restart and pid is not None:
        blocker = restart_blockers()
        if blocker:
            raise RuntimeError(blocker)

    # Build and tests finished before these live mutations. Replace files atomically,
    # keeping one previous binary for manual recovery; an executing inode is untouched.
    if old_binary != new_binary:
        if old_binary is not None:
            atomic_write(runtime / "pam-backend.previous", old_binary, 0o755)
        atomic_write(binary, new_binary, 0o755)
    if old_unit != new_unit:
        atomic_write(unit, new_unit)
    run("systemd-analyze", "--user", "verify", unit)
    if not backend_only:
        archive = build / (WIDGET + ".plasmoid")
        installed = data_home / "plasma/plasmoids" / WIDGET
        if not widget_current(archive, installed):
            action = "--upgrade" if installed.exists() else "--install"
            run("kpackagetool6", "--type", "Plasma/Applet", action, archive)
            print("Widget updated. Loaded native client changes take effect after Plasma restarts or at next login.", flush=True)
    helper.parent.mkdir(parents=True, exist_ok=True)
    if not helper.is_symlink():
        helper.symlink_to(Path(__file__).resolve())
    run("systemctl", "--user", "daemon-reload")
    run("systemctl", "--user", "enable", SERVICE)

    if replacing_manual:
        # The D-Bus owner, executable, and empty device list were checked above.
        # Recheck immediately before signaling this one process, never a process name.
        if owner_pid() != pid or restart_blockers():
            raise RuntimeError("Backend ownership/devices changed during deployment; leaving it running")
        print(f"Handing manually launched backend PID {pid} to systemd", flush=True)
        os.kill(pid, signal.SIGTERM)
        deadline = time.monotonic() + 10
        while owner_pid() is not None:
            if time.monotonic() >= deadline:
                raise RuntimeError("The manual backend has not released D-Bus; no second instance was started")
            time.sleep(0.1)
    if restart and owner_pid() is not None and restart_blockers():
        raise RuntimeError("Backend devices/routes appeared during deployment; leaving it running")
    try:
        if restart:
            run("systemctl", "--user", "restart", SERVICE)
        if service_property("ActiveState") != "active":
            raise RuntimeError("The installed service is not active")
        service_pid = int(service_property("MainPID"))
        if owner_pid() != service_pid:
            raise RuntimeError("The service is not the D-Bus owner")
        # Read a real graph response rather than only checking that a PID exists.
        snapshot = json.loads(bus_call(BUS, OBJECT, BUS, "Snapshot"))
        if snapshot.get("schemaVersion") != 1:
            raise RuntimeError("The installed backend returned an invalid snapshot")
    except Exception:
        if old_binary is not None and old_binary != new_binary:
            atomic_write(binary, old_binary, 0o755)
            subprocess.run(["systemctl", "--user", "restart", SERVICE], check=False)
        raise
    atomic_write(state_home / "deployment.json", json.dumps({
        "source": str(SOURCE), "build": str(build), "backend": str(binary),
        "sha256": hashlib.sha256(new_binary).hexdigest(), "service": str(unit),
    }, indent=2).encode() + b"\n")
    print(f"Running: {binary} (PID {service_pid})", flush=True)
    print("Enabled at graphical login; systemd restarts it after a failure.", flush=True)
    print(f"Editable source: {SOURCE}\nFuture updates: pam-update", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=SOURCE / "build")
    parser.add_argument("--backend-only", action="store_true", help="Deploy only the backend and service; leave the installed widget alone")
    args = parser.parse_args()
    # A host deployment should never inherit an isolated test's session settings.
    if os.environ.get("PAM_TEST_BUS") or os.environ.get("PIPEWIRE_REMOTE") or os.environ.get("PIPEWIRE_RUNTIME_DIR"):
        raise RuntimeError("Run pam-update in your normal desktop session, outside an isolated audio test")
    lock_path = Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp")) / f"pam-update-{os.getuid()}.lock"
    with lock_path.open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        build = args.build_dir.resolve()
        run("cmake", "-S", SOURCE, "-B", build, "-DBUILD_GUI=ON", "-DBUILD_TESTING=ON")
        run("cmake", "--build", build, "--parallel", "2")
        run("ctest", "--test-dir", build, "--output-on-failure")
        deploy(build, args.backend_only)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"Update failed: {error}", file=sys.stderr)
        sys.exit(1)
