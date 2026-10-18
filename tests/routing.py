"""Real samples through isolated native DSP links; no desktop server access."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def exercise(runtime, call, wait_for, nodes, stop, backend):
    log = runtime / "routing-samples.jsonl"
    with log.open("w") as output:
        fixture = subprocess.Popen([sys.argv[sys.argv.index("--routing") + 1]], stdout=output)
    try:
        def snapshot():
            return json.loads(call("Snapshot"))["objects"]

        def node(name):
            return next((n for n in nodes() if n["properties"].get("node.name") == name), None)

        wait_for(lambda: node("test-cable-output"), "routing fixture nodes")
        def ports(name, direction):
            return {p["properties"]["audio.channel"]: p["id"] for p in snapshot()
                    if p["type"].endswith(":Port") and p["properties"].get("node.id") == str(node(name)["id"])
                    and p["properties"].get("port.direction") == direction}
        wait_for(lambda: len(ports("test-app", "out")) == 2 and len(ports("test-output-b", "in")) == 2,
                 "routing DSP ports")
        def key(name):
            return node(name)["properties"]["object.serial"]
        def route(a, b, enabled=True):
            return call("SetRoute", key(a), key(b), str(enabled).lower())
        def rejected(a, b, enabled=True):
            try:
                route(a, b, enabled)
            except subprocess.CalledProcessError:
                return
            raise AssertionError("Invalid route accepted: " + a + " -> " + b)
        def connected(count):
            r = json.loads(call("Routes"))
            return len(r) == count and all(x["state"] == "Connected" for x in r)
        def samples(name, value):
            lines = log.read_text().splitlines()
            if not lines:
                return False
            try:
                stats = json.loads(lines[-1])[name]
                return stats["cycles"] > 0 and all(abs(x - value) < .0001 for x in stats["channels"])
            except (ValueError, KeyError):
                return False
        def links():
            return {o["id"] for o in snapshot() if o["type"].endswith(":Link")}
        baseline = links()
        for channel, source in ports("test-app", "out").items():
            subprocess.run(["pw-link", str(source), str(ports("test-output-a", "in")[channel])], check=True)
        wait_for(lambda: len(links() - baseline) == 2, "external baseline route")
        external = links() - baseline
        wait_for(lambda: samples("test-output-a", .125), "external audio samples")
        rejected("test-app", "test-output-a", False)
        rejected("test-app", "test-output-a")
        matrix = json.loads(call("RoutingSnapshot"))
        row = next(r for r in matrix["sources"] if r["key"] == key("test-app"))
        cell = next(c for c in row["cells"] if c["destination"] == key("test-output-a"))
        assert cell["checked"] and not cell["editable"] and cell["state"] == "External"
        route("test-app", "test-output-b")
        wait_for(lambda: connected(1), "managed stereo links")
        wait_for(lambda: samples("test-output-b", .125), "fan-out audio samples")
        assert samples("test-output-a", .125)
        route("test-app", "test-output-b")
        assert len(json.loads(call("Routes"))) == 1, "Duplicate route"
        route("test-mic", "test-output-b")
        wait_for(lambda: connected(2), "fan-in links")
        wait_for(lambda: samples("test-output-b", .375), "fan-in summed samples")
        route("test-mic", "test-output-b", False)
        wait_for(lambda: samples("test-output-b", .125), "remove only one contribution")
        rejected("test-app", "test-output-mono")
        rejected("test-cable-input", "test-cable-output")
        route("test-mono", "test-output-b")
        wait_for(lambda: samples("test-output-b", .1875), "mono duplicated into stereo")
        call("ClearRoutes")
        wait_for(lambda: links() == baseline | external, "scoped link cleanup")
        assert samples("test-output-a", .125), "External route interrupted"
        if "--routing-gui" in sys.argv:
            index = sys.argv.index("--routing-gui")
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                       QT_QUICK_CONTROLS_STYLE="org.kde.desktop", QT_QPA_PLATFORMTHEME="generic",
                       XDG_CURRENT_DESKTOP="")
            subprocess.run([sys.argv[index + 1], "-input", str(Path(__file__).parent / "qml_routing"),
                            "-import", sys.argv[index + 2]], env=env, check=True, timeout=25)
        route("test-app", "test-output-b")
        wait_for(lambda: connected(1), "route before external link removal")
        subprocess.run(["pw-link", "-d", str(ports("test-app", "out")["FL"]),
                        str(ports("test-output-b", "in")["FL"])], check=True)
        wait_for(lambda: any(r["state"] == "Failed" for r in json.loads(call("Routes"))),
                 "removed channel reported as failed")
        wait_for(lambda: links() == baseline | external, "failed route sibling rollback")
        time.sleep(.4)
        assert all(r["state"] == "Failed" for r in json.loads(call("Routes"))), "Reconciliation fought an external link removal"
        assert links() == baseline | external
        route("test-app", "test-output-b")
        wait_for(lambda: connected(1), "explicit retry of failed route")
        call("ClearRoutes")
        wait_for(lambda: links() == baseline | external, "retry cleanup")
        # Rapid create/remove exercises client-owned proxy teardown before negotiation.
        for _ in range(12):
            route("test-app", "test-output-b")
            route("test-app", "test-output-b", False)
        wait_for(lambda: links() == baseline | external, "rapid route cleanup")
        # Client-owned links must vanish on both orderly exit and SIGKILL;
        # independently owned external links must survive each restart.
        for crash in (False, True):
            route("test-app", "test-output-b")
            wait_for(lambda: connected(1), "route before backend exit")
            stop(backend, kill=crash)
            def cleaned_after_exit():
                graph = json.loads(subprocess.check_output([sys.argv[1], "--snapshot"], text=True, timeout=6))
                return {o["id"] for o in graph["objects"] if o["type"].endswith(":Link")} == baseline | external
            wait_for(cleaned_after_exit, "owned links cleaned after backend exit")
            assert samples("test-output-a", .125), "External audio lost after backend exit"
            backend = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            wait_for(lambda: call("Snapshot"), "backend routing restart")
            wait_for(lambda: connected(1), "saved route restored after backend restart")
            wait_for(lambda: samples("test-output-b", .125), "restored route audio")
        route("test-app", "test-output-b")
        wait_for(lambda: connected(1), "route before endpoint disappearance")
        stop(fixture)
        wait_for(lambda: not json.loads(call("Routes")), "endpoint removal cleanup")
        assert backend.poll() is None, "Routing race stopped backend"
        assert len(json.loads(call("SavedRoutes"))) == 1, "Route intent lost with endpoint"
        call("ClearRoutes")
        print("PASS: measured fan-out/fan-in, mono mapping, ownership, cycle/layout rejection, churn, endpoint removal and normal/crash cleanup")
        return backend
    finally:
        stop(fixture)
