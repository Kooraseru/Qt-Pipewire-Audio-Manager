"""Persistence and identity tests, run only inside lifecycle.py's private servers."""
import json
import os
from pathlib import Path
import subprocess
import sys


def exercise(runtime, call, wait_for, nodes, stop, backend):
    binary = sys.argv[1]
    fixture_binary = sys.argv[sys.argv.index("--persistence") + 1]
    fixture = None
    config = Path(call("ConfigurationPath"))
    assert config.is_relative_to(runtime), "Persistence escaped isolated configuration directory"

    def spawn_backend():
        process = subprocess.Popen([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        wait_for(lambda: call("Snapshot"), "persistent backend restart")
        return process

    def node(name):
        return next((n for n in nodes() if n["properties"].get("node.name") == name), None)

    def controlled(name):
        n = node(name)
        return n if n and n.get("mixer", {}).get("volumeAvailable") else None

    sample_log = runtime / "persistence-samples.jsonl"
    def fixture_start(*args):
        with sample_log.open("w") as output:
            return subprocess.Popen([fixture_binary, *args], stdout=output)

    def samples(name, amplitude):
        try:
            sample = json.loads(sample_log.read_text().splitlines()[-1])[name]
            return sample["cycles"] > 0 and all(abs(x - amplitude) < .0001 for x in sample["channels"])
        except (IndexError, ValueError, KeyError):
            return False

    def route(a, b):
        return call("SetRoute", node(a)["properties"]["object.serial"], node(b)["properties"]["object.serial"], "true")

    def connected(count):
        routes = json.loads(call("Routes"))
        return len(routes) == count and all(r["state"] == "Connected" for r in routes)

    def saved():
        return json.loads(call("SavedRoutes"))

    try:
        token = call("CreatePersistentDevice", "cable", "Remember this cable")
        prefix = "pam.prototype." + token
        for suffix in (".sink", ".source"):
            n = wait_for(lambda: controlled(prefix + suffix), "saved cable mixer")
            key = n["properties"]["object.serial"]
            call("SetVolume", key, "0.125")
            call("SetMute", key, "true")
        assert (config.stat().st_mode & 0o777) == 0o600
        assert json.loads(config.read_text())["devices"][0]["token"] == token
        for crash in (False, True):
            stop(backend, kill=crash)
            graph = json.loads(subprocess.check_output([binary, "--snapshot"], text=True))
            assert not any(o["properties"].get("node.name", "").startswith(prefix) for o in graph["objects"]), "Snapshot restored audio objects"
            backend = spawn_backend()
            for suffix in (".sink", ".source"):
                def restored():
                    n = controlled(prefix + suffix)
                    return n and n["mixer"].get("muted") and abs(n["mixer"]["volume"] - .125) < .001
                wait_for(restored, "cable UUID, volume and mute restored")
            assert json.loads(call("OwnedDevices"))[0]["persistent"]

        fixture = fixture_start()
        wait_for(lambda: node("test-output-b"), "application fixture")
        wait_for(lambda: next((s for s in json.loads(call("RoutingSnapshot"))["sources"] if s["label"] == "test-app" and any(c["editable"] for c in s["cells"])), None), "ready ports")
        old_serial = node("test-app")["properties"]["object.serial"]
        route("test-app", "test-output-a")
        route("test-app", "test-output-b")
        wait_for(lambda: connected(2), "saved fan-out")
        assert len(saved()) == 2
        wait_for(lambda: samples("test-output-a", .125) and samples("test-output-b", .125), "initial fan-out samples")
        assert all("object.serial" not in json.dumps(r["source"]) for r in saved())
        stop(fixture)
        wait_for(lambda: not json.loads(call("Routes")), "old stream removed")
        wait_for(lambda: len(saved()) == 2 and all(r["state"] == "Waiting" for r in saved()), "route intent retained while absent")
        stop(backend, kill=True)
        backend = spawn_backend()
        wait_for(lambda: len(saved()) == 2, "offline routing rules restored")
        fixture = fixture_start("--app-name", "new-stream-and-song-title", "--duplicate-app")
        wait_for(lambda: node("test-app-duplicate"), "ambiguous app fixture")
        wait_for(lambda: all("unique match" in r.get("reason", "") for r in saved()), "ambiguous identity blocked")
        assert not json.loads(call("Routes")), "Guessed between ambiguous application streams"
        stop(fixture)
        wait_for(lambda: node("test-app-duplicate") is None, "duplicate removal")
        fixture = fixture_start("--app-name", "new-stream-and-song-title")
        wait_for(lambda: connected(2), "application routes restored with changed node name")
        wait_for(lambda: samples("test-output-a", .125) and samples("test-output-b", .125), "recreated application fan-out samples")
        assert node("new-stream-and-song-title")["properties"]["object.serial"] != old_serial
        stop(fixture)
        wait_for(lambda: not json.loads(call("Routes")), "offline routes")
        forgotten = saved()[0]["id"]
        call("ForgetRoute", forgotten)
        assert len(saved()) == 1
        stop(backend)
        backend = spawn_backend()
        assert len(saved()) == 1 and saved()[0]["id"] != forgotten
        fixture = fixture_start("--app-name", "third-recreated-stream")
        wait_for(lambda: connected(1), "only remaining route restored")
        call("ClearRoutes")
        assert not saved()
        assert call("DestroyTemporaryDevice", token)
        assert not json.loads(config.read_text())["devices"]
        stop(backend)
        # Read-only snapshot must not load, repair, or overwrite invalid settings.
        original = config.read_bytes()
        for content in (b'{ broken json', b'{"schemaVersion":99,"devices":[],"routes":[]}'):
            config.write_bytes(content)
            subprocess.run([binary, "--snapshot"], stdout=subprocess.DEVNULL, check=True, timeout=6)
            invalid = subprocess.run([binary], capture_output=True, timeout=6)
            assert invalid.returncode != 0 and config.read_bytes() == content, "Invalid settings were overwritten"
        config.write_bytes(original)
        backend = spawn_backend()
        assert not json.loads(call("OwnedDevices")), "Deleted cable returned"
        # A failed atomic write must leave both saved state and the graph unchanged.
        directory = config.parent
        directory.chmod(0o500)
        try:
            try:
                call("CreatePersistentDevice", "sink", "Must not be created")
            except subprocess.CalledProcessError:
                pass
            else:
                raise AssertionError("Unwritable configuration accepted")
            assert config.read_bytes() == original
            assert not json.loads(call("OwnedDevices"))
        finally:
            directory.chmod(0o700)
        print("PASS: saved cable UUID/mixers, normal/crash restart, offline rules, changed app stream identity, ambiguity, forgetting, invalid file preservation and write failure")
        return backend
    finally:
        if fixture is not None:
            stop(fixture)
