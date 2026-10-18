# Qt-Pipewire-Audio-Manager

Native C++/Qt 6 audio manager with a live QML GUI and Plasma 6 plasmoid package.
The backend works independently of Plasma and the graphical client.

Implemented:

- PipeWire registry enumeration and addition/removal notifications, plus node property/state updates.
- Independent backend executable and session D-Bus interface.
- Saved stereo virtual sinks and paired sink/source cables with separate endpoint names; explicit temporary API remains available.
- Deletion restricted to opaque handles allocated by the current backend instance.
- Isolated integration coverage for explicit deletion, normal exit, and crash cleanup.
- GUI based on the supplied PlasmaVolumeBased concept: Devices, Cables, Applications and Routing.
- Native KDE theme icons through Kirigami and Qt Quick Controls; no glyph/emoji icons.
- Live physical input/output lists, virtual endpoints, and playback/recording streams.
- Current stream link inspection, collapsible categories, connection status and errors.
- Create/delete dialogs for backend-owned saved devices; external devices are protected.
- Automatic frontend reconnection after backend restart. Closing the GUI leaves the backend running.
- External monitor/remap endpoints grouped into logical cables using filter-group/target metadata.
- Expandable cable endpoints with independent node volume and mute controls where supported.
- Observed PipeWire node volume/mute, with balance-preserving volume changes and safe serial checks.
- Stale discovery binds handled without stopping the backend or leaving removed nodes in the model.
- A native client bundled into an installable `.plasmoid`, with no global QML-path setup.
- Atomic backend configuration with saved device UUIDs, custom route rules and explicit mixer settings on saved devices.
- Restoration after application stream recreation and backend restart, with ambiguity protection.
- Additive routing matrix and application destination checkboxes backed by owned native links.
- Explicit channel mapping, feedback-cycle checks, locked external routes and scoped route removal.
- Sample-based isolated tests for fan-out, fan-in and mono-to-stereo duplication.
- An optional systemd user service with a stable home-directory binary and tested update command.

Not implemented: independent per-route gain/mute, defaults, renaming, multiple profiles,
meters, or in-process backend reconnect after PipeWire restart. The user service restarts
the backend and restores its saved configuration after a server connection failure.
Recording applications have a persistent source selector backed by WirePlumber target metadata. A device that does not advertise readable,
writable volume/mute parameters retains disabled controls. Idle/suspended nodes are shown
as Idle, which is a normal state and does not disable their available mixer controls.
Cable source endpoints are experimental virtual microphones; application compatibility
and actual audio transport have not yet been validated.

## Add routes

Open **Routing**: rows are physical/virtual sources and playback streams; columns are
physical/virtual output sinks. For a cable, select its **Output** endpoint as the destination;
the cable's **Input** endpoint is the virtual microphone presented to recording applications.
Check a cell to add a connection, uncheck it to remove an Audio Manager connection.
One source can feed several outputs, and several sources can feed one output.
Playback applications also expose destination checkboxes under **Applications → Routes**.
The matrix scrolls horizontally and vertically in the panel popup.

Existing external connections appear checked and locked. This version adds custom branches;
it does not move or replace WirePlumber's ordinary route. **Clear custom routes** removes
only this backend's routes. Selection uses current object serials. Live links disappear when
an endpoint or the backend exits; saved rules reconnect uniquely matching endpoints when
they return. Clearing or deleting a route forgets its rule as well as its live links.

The current native path requires matching positional float DSP channels, with explicit
mono-to-stereo duplication also supported. Unsupported layouts wait for an explicit
remix stage rather than guessing. Each channel must negotiate successfully before the
route reports Connected; errors roll back its links. Sample tests verify unity fan-out
and summed fan-in on the installed PipeWire version. These tests do not establish latency,
CPU cost, clipping protection, hardware clock-domain behavior or WirePlumber coexistence.
Native links have no independent route gain/mute control. Endpoint/stream mute and volume
affect all branches; independent branch processing remains a separate prototype.

Feedback checks include visible links, pending custom routes and known cable internals.
Hidden processing relationships and acoustic feedback cannot be inferred reliably from
the graph. Microphone-to-speaker monitoring requires care.

## Select an application's microphone

Start recording/voice chat in the application so its capture stream exists. Open
**Applications → Recording → the application's Routes → Input source**, then select
a physical microphone or a cable's **Input** endpoint. For example:

`YouTube Music → VC1 Output → VC1 Input → Sober recording stream`

The playback connection is selected in the routing matrix. The recording dropdown moves
one capture stream through WirePlumber's supported `target.object` metadata on the
`default` metadata object. It does not create parallel microphone links, change global
defaults, manipulate unrelated links or change source mute/volume. WirePlumber owns the
actual input links, including channel conversion. No Sober/Roblox-specific integration
or modification of PipeWire/WirePlumber configuration is used.

The selected input is saved separately under `recordingAssignments`, with the same stable
application matching and ambiguity checks as playback routing. The backend reapplies it
once per new stream/source/metadata generation. It does not repeatedly override a later
move made by another controller. The dropdown shows saved intent; **Current sources**
shows observed links. Requested does not mean linked successfully. If a saved microphone
is missing, WirePlumber may fall back to another source; this is reported explicitly.
There is no guarantee against initial/default-source capture before restoration.

**Use default input (forget assignment)** removes the saved input selection and asks
WirePlumber to use normal default selection for that stream. **Routing → Saved recording
inputs** also lets you forget offline assignments. Deleting a saved project microphone
forgets assignments referring to it. Capture streams marked `node.dont-move`, lacking a
stable identity, or having ambiguous identities are not silently reassigned.

Tests use a separate WirePlumber `policy` profile with no hardware monitors and private
configuration/state. They verify actual samples through two cables into real recording
clients, exclusive source switching, stream/backend crash restoration, and independence
of cable source mute. The GUI test operates the input dropdown through its native menu items.
Actual Sober/Roblox voice reception, real hardware microphone switching and hardware
profile/clock-domain combinations still need an interactive test. Independent **per-route**
gain/mute is still not implemented; a source mute affects everyone recording that source.

## Saved configuration

The backend owns `~/.config/plasma-audio-manager/config.json` (or the equivalent under
`XDG_CONFIG_HOME`). It is versioned JSON, written atomically with owner-only permissions
and protected by a single-writer lock. Invalid JSON/unknown versions are preserved and
cause startup to fail rather than replacing your settings. Tests use an isolated config
home as well as private PipeWire and D-Bus servers.

Saved data includes:

- Created cable/sink UUID, kind and label. The same UUID gives the same endpoint names at login.
- Volume and mute explicitly set through this widget on **saved project devices**.
- Explicit custom routing rules, source/destination selectors and display labels.

Application selectors use an advertised application ID or process binary, plus advertised
application name, role and restore ID. They exclude PID, PipeWire global IDs/serials,
stream names and song titles. More than one matching source/destination pauses the rule
rather than guessing or broadcasting to every matching stream. Device selectors use node
names and advertised hardware identifiers; a profile/name change may need reselection.
External VC1–VC3 remain external targets and are never imported into the device list.

Rules wait for endpoints and DSP ports, re-run channel/feedback checks, and restore on
node/port recreation. They do not continually recreate a failed link on unchanged ports;
explicitly toggle/retry it to override an external removal. Ordinary WirePlumber routes
remain locked and untouched. Restoration is asynchronous and does not suppress the
ordinary default route or guarantee that no audio reaches it before restoration.

**Routing → Saved routes** shows rules even when their application is absent, and its
trash button forgets a rule. Deleting a saved device also deletes rules referring to its
endpoints. **Forget all custom routes** clears routing rules without deleting devices.
Physical-device/application mixer restoration remains WirePlumber's responsibility;
per-route gain/mute and multiple named profiles are separate future work.

`ConfigurationPath()` and `SavedConfiguration()` expose the backend-owned path/document
for inspection over D-Bus. For manual edits, stop the backend, edit the file, then restart
it; edits are loaded at startup, not watched live. This file is separate from all PipeWire
and WirePlumber configuration. Existing lost routes cannot be recovered retroactively;
new route selections are saved immediately.

## Build and run

Requires CMake, a C++20 compiler, pkg-config, Qt 6 Core/DBus, and libpipewire development
files. The GUI additionally needs Qt Quick/Quick Controls 2, KF6 Kirigami and the
`org.kde.desktop` Qt Quick Controls style (Arch: `kirigami` and `qqc2-desktop-style`).
The plasmoid host uses libplasma. Tests additionally use Python 3, `pipewire`, `gdbus`,
`dbus-run-session`, and Qt 6's `qmltestrunner`. Set `-DBUILD_GUI=OFF` for a backend-only
build with no KDE dependencies. No Rust, libpulse or WirePlumber library is required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/pam-backend --snapshot
```

Launch the backend and GUI in separate terminals:

```sh
./build/pam-backend
./build/pam-gui
```

The GUI connects to the running backend on the session bus. It does not start an audio
service, stop the backend when closed, or show sample devices when disconnected.
For normal use, deploy the service below rather than keeping a development terminal open.
The shared production UI lives in `plasmoid/package/contents/ui` and is embedded into
the standalone app, so the two hosts use the same components.

For a screenshot of the running GUI:

```sh
./build/pam-gui --tab cables --capture /tmp/audio-manager.png
```

`--tab` accepts `devices`, `cables`, `applications`, or `routing`; capture exits after taking the image.
It only observes the existing graph. No preview installation or user-service activation
is performed by building or running tests.

## Plasma package

`plasmoid/package` uses its own ID, `org.kooraseru.plasma.audiomanager`, and provides
an icon in the panel plus the expanded popup. Build and install it for the current user:

```sh
cmake --build build --target install-widget
```

Then open Plasma's **Add Widgets** chooser, search for **Plasma Audio Manager**, and
drag it onto your panel. The widget-only install does not install a backend service.
Use the update command below for startup support, or start `./build/pam-backend` manually.
Restarting PipeWire is unnecessary.

To build the installable archive without installing it:

```sh
cmake --build build --target plasmoid-package
```

This produces `build/org.kooraseru.plasma.audiomanager.plasmoid`. It bundles the compiled
QML client in `contents/ui/PlasmaAudioManager`, using a relative import and library paths
relative to the plugin. No global import-path or Plasma environment changes are required.
The archive is a native build for this architecture/Qt version, rather than a portable
QML-only download. Installing the raw source directory omits the native module; use the
built archive or the install target. The `install-widget` target upgrades an existing
user installation under the same ID.

The package has been loaded successfully by Plasma's `plasmawindowed --smoke-test` host
from a staged data directory with the bundled native module. Actual panel placement,
popup behavior and keyboard navigation still need an interactive desktop check.

Review a staged installation without installing into your desktop:

```sh
DESTDIR=/tmp/pam-stage cmake --install build --prefix /usr
```

The native module goes to `lib/qt6/qml/PlasmaAudioManager` relative to the chosen prefix
(override `PAM_QML_INSTALL_DIR` for another Qt layout) for the standalone GUI. The plasmoid
goes to `share/plasma/plasmoids/org.kooraseru.plasma.audiomanager` and has its own module copy.

`--snapshot` is read only and prints JSON. Without it, the process restores its saved
configuration and serves explicit D-Bus requests. Startup service deployment is explicit and does not write audio configuration.

## Startup and ongoing development

Run this from the editable source checkout for initial deployment:

```sh
./tools/update.py
```

Afterward, the same operation is available from any directory:

```sh
pam-update
```

It configures/builds this checkout, runs the test suite, atomically deploys the backend,
updates the widget if its files changed, enables the user service and verifies the live
D-Bus graph response. The source stays in this repository; startup runs an independent
binary in the home directory, so it does not depend on this mount or the build folder
being available at login. The helper symlink requires the source checkout to be available
only when building future changes. `pam-update --backend-only` leaves the widget untouched.

| Purpose | Location |
| --- | --- |
| Runtime backend | `~/.local/libexec/plasma-audio-manager/pam-backend` |
| Previous deployed binary, after an update | `~/.local/libexec/plasma-audio-manager/pam-backend.previous` |
| User service | `~/.config/systemd/user/plasma-audio-manager.service` |
| Editable service source | `systemd/plasma-audio-manager.service` in this repository |
| Update helper | `~/.local/bin/pam-update` → this checkout's `tools/update.py` |
| Backend configuration | `~/.config/plasma-audio-manager/config.json` |
| Deployment record | `~/.local/state/plasma-audio-manager/deployment.json` |
| Installed widget | `~/.local/share/plasma/plasmoids/org.kooraseru.plasma.audiomanager/` |

Configuration/data/state paths follow their XDG environment overrides when set. The service
runs during the graphical user session, starts at login, stops at logout, and restarts after
a failure. It waits for the backend's D-Bus name before reporting readiness, and retries
every three seconds if PipeWire is temporarily unavailable. It does not enable user lingering
or manage/restart PipeWire and WirePlumber. No audio configuration is installed or rewritten.

```sh
systemctl --user status plasma-audio-manager
journalctl --user -u plasma-audio-manager -f
systemctl --user restart plasma-audio-manager
```

To stop startup and the running backend:

```sh
systemctl --user disable --now plasma-audio-manager
```

The updater replaces only a recognized manual backend, verifies its D-Bus PID/executable,
and refuses a restart while custom routes or connected/running saved devices are in use.
Idle saved devices may be recreated safely from their stable definitions. Explicit temporary
devices still block restart because they have no saved definition. A no-change update leaves
the running backend alone. A failed new binary deployment restores the previous binary
when one exists. Concurrent updates are serialized with a runtime lock. Loaded native widget
client changes take effect after a Plasma restart or next login; the updater does not restart
the desktop. Changes confined to the backend apply through its supervised restart.

```sh
./build/pam-backend
# From another terminal:
gdbus call --session --dest org.plasmaaudiomanager.Prototype1 \
  --object-path /org/plasmaaudiomanager/Prototype1 \
  --method org.plasmaaudiomanager.Prototype1.Snapshot
```

`RoutingSnapshot()` returns matrix endpoints/cells, including checked/editable state and
rejection reasons. `Routes()` returns owned routes with Connecting/Connected/Failed state.
`SetRoute(sourceSerial, destinationSerial, enabled)` adds/removes a scoped route, and
`SetRecordingSource(streamSerial, sourceSerial)` saves/selects a recording input; an empty
source clears that assignment and selects default behavior. `ForgetRecordingAssignment(id)`
forgets an offline input rule. Recording options/status appear under `RoutingSnapshot.recording`.
`ClearRoutes()` forgets all custom playback route rules and removes their links. `SavedRoutes()`
includes offline rules and resolution status; `ForgetRoute(id)` deletes one saved rule. `RoutesChanged` notifies clients. The return
from SetRoute acknowledges the request; link-state callbacks confirm negotiation.

The same interface exposes `OwnedDevices()` for current backend-owned handle metadata,
`CreatePersistentDevice(kind, label)` (the GUI default) or the explicit development-only
`CreateTemporaryDevice(kind, label)`, where kind is `sink`
or `cable`, and `DestroyTemporaryDevice(token)`. Creation returns an accepted request
token, not an audio readiness guarantee. Registry events confirm endpoint appearance.
Cable names are `pam.prototype.<token>.sink` and `.source`; plain sinks omit the suffix.
Labels are descriptive, while tokens provide identity. Live devices disappear when their
owning backend exits, including after SIGKILL. Saved definitions recreate the same node
names on the next backend start; temporary definitions do not. Creating them is opt-in; startup restores only previously saved project devices. `GraphChanged` emits JSON events; `Snapshot` returns schema
version 1 and all visible registry objects. Only nodes have property/state subscriptions.
`SetVolume(serial, gain)` and `SetMute(serial, muted)` control a live endpoint's node
Props. Identity uses the current `object.serial`, so a removed/reused global ID cannot
redirect a control to another device. Volume gain is linear amplitude in [0, 1]; the UI
uses a cubic slider curve (50% corresponds to 0.125 amplitude). Channel ratios are
preserved. State comes back through subscribed Props, rather than optimistic local values.
`OperationFailed` reports asynchronous object errors; genuine core/connection failures
still stop the backend. These endpoint controls do not implement independent route gain.
The interface remains experimental.

Tests run a separate PipeWire server and D-Bus session in a temporary runtime directory.
The GUI test clicks tabs, creates/deletes a cable through the dialogs, checks external
device protection, resizes the popup, and verifies disconnect/reconnect against a restarted
backend. Tests also verify three metadata-paired external cables, endpoint mute/volume
updates, external volume observation, preservation of channel balance, rejection of stale
identities, and a deterministic delete-before-bind race. Routing tests measure actual
float samples through stereo fan-out, summed fan-in and mono duplication, exercise matrix
and application checkboxes, preserve external links, reject cycles/unsupported layouts,
and verify route churn, endpoint disappearance and normal/crash cleanup. Test-bus activation is disabled
so desktop portal daemons are not started.
They do not connect to the desktop audio graph, start a device monitor/session manager,
or load the user's audio configuration. See [architecture](docs/architecture.md) for
ownership boundaries, lifecycle alternatives, and the processing experiment still needed.
