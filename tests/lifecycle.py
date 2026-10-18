"""Integration test: separate runtime dir, PipeWire server and session bus.

No device monitor, session manager, physical device, or host configuration loaded.
"""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

SERVICE = "org.plasmaaudiomanager.Prototype1"
OBJECT = "/org/plasmaaudiomanager/Prototype1"


def wait_for(function, description):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            value = function()
            if value:
                return value
        except (subprocess.SubprocessError, SyntaxError):
            pass
        time.sleep(0.05)
    raise AssertionError("Timed out: " + description)


def call(method, *args):
    result = subprocess.check_output([
        "gdbus", "call", "--session", "--dest", SERVICE, "--object-path", OBJECT,
        "--method", SERVICE + "." + method, *args], stderr=subprocess.DEVNULL,
        text=True, timeout=2)
    # gdbus represents booleans as lowercase; strings/snapshots use Python quoting.
    if result.strip() in ("(true,)", "(false,)"):
        return result.strip() == "(true,)"
    return ast.literal_eval(result)[0]


def nodes():
    return [obj for obj in json.loads(call("Snapshot"))["objects"]
            if obj["type"].endswith(":Node")]


def project_nodes():
    return [obj for obj in nodes()
            if obj["properties"].get("node.name", "").startswith("pam.prototype.")]


def stop(process, kill=False):
    if process.poll() is None:
        process.kill() if kill else process.terminate()
    process.wait(timeout=5)


def run(binary):
    server = backend = monitor = reconnect = None
    with tempfile.TemporaryDirectory(prefix="pam-isolated-") as directory:
        runtime = Path(directory)
        os.environ["XDG_STATE_HOME"] = str(runtime / "state")
        os.environ["XDG_CONFIG_HOME"] = str(runtime / "config")
        os.environ["XDG_RUNTIME_DIR"] = directory
        os.environ["PIPEWIRE_RUNTIME_DIR"] = directory
        os.environ["PIPEWIRE_REMOTE"] = "pam-test"
        # Prevent client context fragments from the user's audio configuration.
        os.environ["PIPEWIRE_CONFIG_DIR"] = "/usr/share/pipewire"
        os.environ["PIPEWIRE_CONFIG_NAME"] = "client.conf"
        config = runtime / "server.conf"
        server_config = '''
context.properties = { core.daemon = true core.name = pam-test }
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.* = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-access args = { access.force = unrestricted } }
    { name = libpipewire-module-spa-node-factory }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-link-factory }
]
context.objects = [
    { factory = spa-node-factory args = {
        factory.name = support.node.driver
        node.name = Dummy-Driver
        node.group = pipewire.dummy
        priority.driver = 20000
    } }
    { factory = adapter args = {
        factory.name = support.null-audio-sink
        node.name = untouched-sentinel
        media.class = Audio/Sink
        audio.position = [ FL FR ]
    } }
]
'''
        # Legacy-style monitor/remap cables: relationships are metadata, not names.
        modules = ""
        sinks = ""
        for number in range(1, 4):
            modules += f'''
    {{ name = libpipewire-module-loopback args = {{
        capture.props = {{
            node.name = input.legacy{number}
            node.link-group = legacy{number}
            node.virtual = true
            node.autoconnect = false
            stream.capture.sink = true
            target.object = legacy{number}.sink
        }}
        playback.props = {{
            node.name = legacy{number}.source
            node.description = "VC{number} Input"
            node.link-group = legacy{number}
            node.virtual = true
            node.autoconnect = false
            media.class = Audio/Source
        }}
    }} }}
'''
            sinks += f'''
    {{ factory = adapter args = {{
        factory.name = support.null-audio-sink
        node.name = legacy{number}.sink
        node.description = "VC{number}"
        media.class = Audio/Sink
        audio.position = [ FL FR ]
    }} }}
'''
        server_config = server_config.replace(
            "    { name = libpipewire-module-link-factory }\n]",
            "    { name = libpipewire-module-link-factory }\n" + modules + "]")
        server_config = server_config.rsplit("]", 1)[0] + sinks + "]\n"
        config.write_text(server_config)
        try:
            # Server selects its explicit config; clients use the stock client config.
            env = os.environ.copy()
            env.pop("PIPEWIRE_CONFIG_NAME")
            env.pop("PIPEWIRE_CONFIG_DIR")
            server = subprocess.Popen(["pipewire", "-c", str(config)], env=env,
                                      stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            wait_for(lambda: (runtime / "pam-test").exists(), "isolated server")
            if "--race" in sys.argv:
                subprocess.run([sys.argv[sys.argv.index("--race") + 1]], check=True, timeout=8)
            snapshot = json.loads(subprocess.check_output([binary, "--snapshot"], text=True, timeout=6))
            assert any(o["properties"].get("node.name") == "untouched-sentinel"
                       for o in snapshot["objects"])
            backend = subprocess.Popen([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            wait_for(lambda: call("Snapshot"), "backend DBus registration")
            sentinel = next(o for o in nodes() if o["properties"].get("node.name") == "untouched-sentinel")
            monitor_file = runtime / "signals.log"
            with monitor_file.open("w") as output:
                monitor = subprocess.Popen(["gdbus", "monitor", "--session", "--dest", SERVICE,
                    "--object-path", OBJECT], stdout=output, stderr=subprocess.DEVNULL)
                wait_for(lambda: "owned by" in monitor_file.read_text(), "signal subscription")
                assert call("DestroyTemporaryDevice", str(sentinel["id"])) is False
                invalid = subprocess.run(["gdbus", "call", "--session", "--dest", SERVICE,
                    "--object-path", OBJECT, "--method", SERVICE + ".CreateTemporaryDevice",
                    "invalid", "test"], capture_output=True)
                assert invalid.returncode != 0
                for kind, count in [("sink", 1), ("cable", 2)]:
                    token = call("CreateTemporaryDevice", kind, 'Test " label with \\ and unicode: 音')
                    wait_for(lambda: len(project_nodes()) == count, kind + " creation")
                    assert all(token in n["properties"]["node.name"] for n in project_nodes())
                    if kind == "cable":
                        assert {n["properties"].get("media.class") for n in project_nodes()} == {
                            "Audio/Sink", "Audio/Source"}
                    if kind == "sink":
                        def controlled():
                            return next((n for n in project_nodes() if n.get("mixer", {}).get("volumeAvailable")), None)
                        node = wait_for(controlled, "observable node mixer")
                        serial = node["properties"]["object.serial"]
                        subprocess.run(["pw-cli", "set-param", str(node["id"]), "Props",
                            "{ channelVolumes = [ 0.5 0.25 ] }"], check=True, capture_output=True, timeout=3)
                        wait_for(lambda: len(controlled()["mixer"].get("channels", [])) == 2 and
                                 abs(controlled()["mixer"]["channels"][1] - 0.25) < 0.001,
                                 "external volume observation")
                        assert call("SetVolume", serial, "0.125")
                        wait_for(lambda: abs(controlled()["mixer"]["volume"] - 0.125) < 0.001,
                                 "volume round trip")
                        assert abs(controlled()["mixer"]["channels"][1] - 0.0625) < 0.001, "Channel balance changed"
                        assert call("SetMute", serial, "true")
                        wait_for(lambda: controlled()["mixer"]["muted"], "mute round trip")
                        assert call("SetMute", serial, "false")
                        wait_for(lambda: not controlled()["mixer"]["muted"], "unmute round trip")
                    assert call("DestroyTemporaryDevice", token)
                    assert call("DestroyTemporaryDevice", token) is False
                    wait_for(lambda: len(project_nodes()) == 0, kind + " destruction")
                    if kind == "sink":
                        rejected = subprocess.run(["gdbus", "call", "--session", "--dest", SERVICE,
                            "--object-path", OBJECT, "--method", SERVICE + ".SetMute", serial, "true"],
                            capture_output=True, timeout=3)
                        assert rejected.returncode != 0, "Stale identity was accepted"
                wait_for(lambda: '"event":"removed"' in monitor_file.read_text(), "removal signal")
                assert "GraphChanged" in monitor_file.read_text()
            stop(monitor)
            if "--recording" in sys.argv:
                from recording import exercise
                backend = exercise(runtime, call, wait_for, nodes, stop, backend)
                return
            if "--persistence" in sys.argv:
                from persistence import exercise
                backend = exercise(runtime, call, wait_for, nodes, stop, backend)
            if "--routing" in sys.argv:
                from routing import exercise
                backend = exercise(runtime, call, wait_for, nodes, stop, backend)
            if "--gui" in sys.argv:
                index = sys.argv.index("--gui")
                gui_env = os.environ.copy()
                gui_env.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                               QT_QUICK_CONTROLS_STYLE="org.kde.desktop",
                               QT_QPA_PLATFORMTHEME="generic", XDG_CURRENT_DESKTOP="")
                subprocess.run([sys.argv[index + 1], "-input",
                    str(Path(__file__).parent / "qml"), "-import", sys.argv[index + 2]],
                    env=gui_env, check=True, timeout=30)
                reconnect = subprocess.Popen([sys.argv[index + 1], "-input",
                    str(Path(__file__).parent / "qml_reconnect"), "-import", sys.argv[index + 2]],
                    env=gui_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                def marker(expected):
                    for line in reconnect.stdout:
                        print(line, end="", flush=True)
                        if expected in line:
                            return
                    raise AssertionError("Missing reconnect marker: " + expected)
                marker("PAM_GUI_CONNECTED")
                stop(backend)
                marker("PAM_GUI_OFFLINE")
                backend = subprocess.Popen([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                output, _ = reconnect.communicate(timeout=15)
                print(output, end="")
                assert reconnect.returncode == 0, "GUI did not reconnect after backend restart"
                wait_for(lambda: call("Snapshot"), "backend after GUI reconnect test")
            for crash in [False, True]:
                call("CreateTemporaryDevice", "sink", "Cleanup sink")
                call("CreateTemporaryDevice", "cable", "Cleanup cable")
                wait_for(lambda: len(project_nodes()) == 3, "cleanup test objects")
                assert next(o for o in nodes() if o["id"] == sentinel["id"]) == sentinel
                stop(backend, kill=crash)
                # Independent registry client verifies cleanup after owner disappearance.
                def clean():
                    data = json.loads(subprocess.check_output([binary, "--snapshot"], text=True, timeout=6))
                    return not any(o["properties"].get("node.name", "").startswith("pam.prototype.")
                                   for o in data["objects"])
                wait_for(clean, "crash cleanup" if crash else "normal cleanup")
                if not crash:
                    backend = subprocess.Popen([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    wait_for(lambda: call("Snapshot"), "backend restart")
            print("PASS: discovery, DBus events, validation, scoped deletion, normal/crash cleanup")
        finally:
            for process in [reconnect, monitor, backend, server]:
                if process is not None:
                    stop(process)


if __name__ == "__main__":
    if os.environ.get("PAM_TEST_BUS") != "1":
        env = os.environ.copy()
        env["PAM_TEST_BUS"] = "1"
        with tempfile.TemporaryDirectory(prefix="pam-test-bus-") as bus_directory:
            bus_config = Path(bus_directory) / "session.conf"
            # No activation directories: UI tests must not start desktop portal daemons.
            bus_config.write_text('''<busconfig>
<type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
<policy context="default"><allow send_destination="*"/>
<allow receive_sender="*"/><allow own="*"/></policy>
</busconfig>''')
            sys.exit(subprocess.call(["dbus-run-session", "--config-file", str(bus_config),
                "--", sys.executable, __file__, str(Path(sys.argv[1]).resolve()), *sys.argv[2:]], env=env))
    run(sys.argv[1])
