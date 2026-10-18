"""Recording moves through actual WirePlumber policy in a private server."""
import array
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def exercise(runtime, call, wait_for, nodes, stop, backend):
    processes = []
    wp_log = runtime / "wireplumber.log"
    def node(name):
        return next((n for n in nodes() if n['properties'].get('node.name') == name), None)
    def key(name):
        return node(name)['properties']['object.serial']
    def recording():
        return json.loads(call('RoutingSnapshot'))['recording']
    def connected(name, source):
        graph = json.loads(call('Snapshot'))['objects']
        links = [o['properties'] for o in graph if o['type'].endswith(':Link') and o['properties'].get('link.input.node') == str(node(name)['id'])]
        return links and all(l.get('link.output.node') == str(node(source)['id']) for l in links)
    def capture(name, appid):
        path = runtime / (name + '.raw')
        p = subprocess.Popen(['pw-record', '--raw', '--format', 'f32', '--rate', '48000', '--channels', '2',
            '--latency', '256', '--properties', f'{{ node.name = "{name}" node.description = "{name}" application.id = "{appid}" }}', str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(p)
        wait_for(lambda: node(name), 'recording stream ' + name)
        return p, path
    def samples(path, value):
        data = path.read_bytes() if path.exists() else b''
        if len(data) < 8192:
            return False
        values = array.array('f'); values.frombytes(data[-4096:])
        return all(abs(x - value) < .001 for x in values)
    try:
        # Stock policy components, no ALSA/Bluetooth/video monitors or host state.
        wp_config = runtime / 'wireplumber.conf'
        wp_config.write_text(Path('/usr/share/wireplumber/wireplumber.conf').read_text() + '''
wireplumber.profiles = {
    pam-test = {
        inherits = [ policy, mixin.systemwide-session ]
        support.dbus = disabled
        support.mpris = disabled
    }
}
''')
        env = os.environ.copy()
        env['WIREPLUMBER_CONFIG_DIR'] = str(runtime)
        env['XDG_STATE_HOME'] = str(runtime / 'state')
        # WirePlumber uses its explicit context config; PW clients still use stock client.conf.
        env.pop('PIPEWIRE_CONFIG_NAME', None); env.pop('PIPEWIRE_CONFIG_DIR', None)
        with wp_log.open('w') as log:
            wp = subprocess.Popen(['wireplumber', '-c', str(wp_config), '-p', 'pam-test'], env=env, stdout=log, stderr=log)
        processes.append(wp)
        try:
            wait_for(lambda: recording()['available'], 'WirePlumber metadata')
        except Exception:
            print(wp_log.read_text()); raise
        fixture = subprocess.Popen([sys.argv[sys.argv.index('--recording') + 1]], stdout=subprocess.DEVNULL)
        processes.append(fixture)
        wait_for(lambda: node('test-mic'), 'audio fixture')
        a = call('CreatePersistentDevice', 'cable', 'Cable A')
        b = call('CreatePersistentDevice', 'cable', 'Cable B')
        a_sink, a_source = 'pam.prototype.' + a + '.sink', 'pam.prototype.' + a + '.source'
        b_sink, b_source = 'pam.prototype.' + b + '.sink', 'pam.prototype.' + b + '.source'
        wait_for(lambda: node(a_source) and node(b_source), 'saved cables')
        def route_ready():
            matrix = json.loads(call('RoutingSnapshot'))
            row = next((r for r in matrix['sources'] if r['key'] == key('test-app')), None)
            return row and next((c for c in row['cells'] if c['destination'] == key(a_sink) and c['editable']), None)
        wait_for(route_ready, 'cable DSP ports')
        call('SetRoute', key('test-app'), key(a_sink), 'true')
        call('SetRoute', key('test-mic'), key(b_sink), 'true')
        recorder, path = capture('recording-app', 'org.pam.RecordingTest')
        other, other_path = capture('unrelated-recorder', 'org.pam.OtherRecorder')
        call('SetRecordingSource', key('recording-app'), key(a_source))
        call('SetRecordingSource', key('unrelated-recorder'), key(b_source))
        wait_for(lambda: connected('recording-app', a_source) and connected('unrelated-recorder', b_source), 'exclusive input moves')
        wait_for(lambda: samples(path, .125) and samples(other_path, .25), 'YouTube-style signal through cable to recording client')
        # A source mute must not alter another cable's source or signal.
        call('SetMute', key(a_source), 'true')
        wait_for(lambda: samples(path, 0) and samples(other_path, .25), 'independent cable source mute')
        assert not node(b_source)['mixer']['muted']
        call('SetMute', key(a_source), 'false')
        wait_for(lambda: samples(path, .125), 'cable unmute')
        if '--recording-gui' in sys.argv:
            index = sys.argv.index('--recording-gui')
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', QT_QUICK_CONTROLS_STYLE='org.kde.desktop', QT_QPA_PLATFORMTHEME='generic', XDG_CURRENT_DESKTOP='')
            subprocess.run([sys.argv[index + 1], '-input', str(Path(__file__).parent / 'qml_recording'), '-import', sys.argv[index + 2]], env=env, check=True, timeout=25)
            wait_for(lambda: connected('recording-app', a_source), 'GUI selection confirmed')
        old = key('recording-app')
        stop(recorder)
        wait_for(lambda: node('recording-app') is None, 'capture disappearance')
        recorder, path = capture('recreated-recording-app', 'org.pam.RecordingTest')
        wait_for(lambda: connected('recreated-recording-app', a_source), 'saved input assignment on new stream')
        wait_for(lambda: samples(path, .125), 'restored capture samples')
        assert key('recreated-recording-app') != old
        # A backend crash must recreate the cables and restore the application's
        # selected source without restarting the recording clients or WirePlumber.
        stop(backend, kill=True)
        backend = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        wait_for(lambda: call('Snapshot'), 'backend recording restart')
        wait_for(lambda: node(a_source) and node(b_source), 'cable recreation')
        wait_for(lambda: connected('recreated-recording-app', a_source) and connected('unrelated-recorder', b_source), 'recording assignments after backend crash')
        wait_for(lambda: samples(path, .125) and samples(other_path, .25), 'restored cable and capture audio')
        call('SetRecordingSource', key('recreated-recording-app'), key(b_source))
        try:
            wait_for(lambda: connected('recreated-recording-app', b_source) and samples(path, .25), 'second microphone source selection')
        except Exception:
            print('RECORDING', json.dumps(recording()))
            print('LINKS', json.dumps([o for o in json.loads(call('Snapshot'))['objects'] if o['type'].endswith(':Link')]))
            print('MIC', json.dumps(node('test-mic')))
            print('WP', wp_log.read_text())
            raise
        # Returning to the default forgets only this application's saved assignment.
        call('SetRecordingSource', key('recreated-recording-app'), '')
        assert len(recording()['assignments']) == 1
        assert connected('unrelated-recorder', b_source)
        saved = json.loads(Path(call('ConfigurationPath')).read_text())
        assert len(saved['recordingAssignments']) == 1
        call('ForgetRecordingAssignment', saved['recordingAssignments'][0]['id'])
        call('ClearRoutes')
        stop(recorder); stop(other)
        call('DestroyTemporaryDevice', a); call('DestroyTemporaryDevice', b)
        print('PASS: real WirePlumber recording moves, exclusive source links, cable audio transport, independent mute, stream recreation and default reset')
        return backend
    finally:
        for p in reversed(processes): stop(p)
