"""Install into temporary data directories and load with the actual Plasma host."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

widget = "org.kooraseru.plasma.audiomanager"
with tempfile.TemporaryDirectory(prefix="pam-widget-") as directory:
    root = Path(directory)
    env = os.environ.copy()
    env.update(XDG_DATA_HOME=str(root / "share"), XDG_CONFIG_HOME=str(root / "config"),
               XDG_CACHE_HOME=str(root / "cache"), QT_QPA_PLATFORM="offscreen",
               QT_QUICK_BACKEND="software", QT_QUICK_CONTROLS_STYLE="org.kde.desktop",
               QT_QPA_PLATFORMTHEME="generic", QT_LOGGING_TO_CONSOLE="1", QML_IMPORT_TRACE="1")
    env.pop("QML_IMPORT_PATH", None)
    env.pop("QML2_IMPORT_PATH", None)
    subprocess.run([sys.argv[3], "--type", "Plasma/Applet", "--install", sys.argv[1]],
                   env=env, check=True, capture_output=True, text=True, timeout=10)
    bus = root / "bus.conf"
    bus.write_text('''<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>
<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*"/>
<allow receive_sender="*"/><allow own="*"/></policy></busconfig>''')
    result = subprocess.run(["dbus-run-session", "--config-file", str(bus), "--",
                             sys.argv[2], "--smoke-test", widget], env=env,
                            capture_output=True, text=True, timeout=15)
    log = result.stdout + result.stderr
    errors = re.findall(r"^.*(?:ReferenceError|TypeError|is not installed|is not a type|Type .* unavailable|Cannot load|Error loading).*$", log, re.M)
    assert result.returncode == 0 and not errors, log
    assert "/contents/ui/PlasmaAudioManager/libpam_frontendplugin.so" in log, log
    assert "/contents/ui/AudioPopup.qml" in log, log
    print("PASS: installed package loads in Plasma with its bundled native module")
