# Architecture decisions and experiments

## Review incorporated

C++/Qt is provisional. libpipewire, session D-Bus, an independent backend process,
and backend-owned persistence are approved directions. The user has now explicitly requested
a continuously running backend with login startup, implemented as an optional user service.
A custom replacement for WirePlumber policy and a per-route processing topology remain undecided.
The source layout contains implemented backend concerns, a D-Bus QML client under
`frontend`, and the shared popup inside `plasmoid/package`. The initial native routing engine lives under `backend/routing`; persistence lives under `backend/persistence`. The frontend builds on the supplied PlasmaVolumeBased
concept, using native KDE theme icons rather than character glyphs.

## Lifecycle follows the requirement

The original lifecycle prototype ran on demand and owned only temporary definitions;
the current backend also restores saved definitions while retaining client-owned runtime objects. A server-side
adapter supplies a standalone null sink; a module loaded in the client context supplies
a paired cable with sink and source endpoints. This tests object ownership without
deciding how later independent gain stages will work. No audio DSP is implemented here.

Persistence has two meanings: surviving closure of a UI and restoring desired state
after login or an audio-server restart. An independent on-demand process meets the
first while it remains running; it does not establish that an always-running service
is needed for either meaning.

| Requirement | Candidate | Remaining experiment |
| --- | --- | --- |
| Temporary devices | Process-owned nodes/modules | Tested normal and crash cleanup on isolated server |
| Devices after controller exit | Server-hosted lingering objects | Test ownership, stale-object discovery and cleanup; client loopback cannot be assumed to survive |
| Recreate saved devices at login | Short-lived restore command or WirePlumber component | Test readiness ordering and idempotence |
| Reconcile arbitrary recreated streams | Resident observer or WirePlumber hooks | Test policy cooperation, matching and first-buffer leakage |
| UI-independent control | Independent D-Bus process | Manual launch or explicitly deployed user service; GUI survives backend restarts |

The user selected continuous operation and login startup after testing the widget, so the
deployment now includes a graphical-session user service. The backend itself retains the
ability to run independently and on demand. An optional WirePlumber component could host policy observation instead, but installing
one changes session-manager configuration and requires separate explicit approval.

## Ownership boundary

WirePlumber retains physical devices, profiles, default selection, ordinary stream routing,
and ordinary stream restoration. Explicit user volume/mute requests change a selected
endpoint's advertised node Props. The application does not change global policy settings,
default metadata, hardware profiles, or ordinary stream links.

The application owns only objects it allocates. Virtual-device deletion accepts its own
opaque token, never arbitrary PipeWire IDs or names. Existing VC1–VC3 are not adopted or
migrated. Their paired presentation uses a unique monitor-capture target and matching
filter link-group; names alone never establish a pair. Unknown or ambiguous relationships
remain individual endpoints. Pairing does not transfer lifecycle ownership. Low session priority and disabled autoconnect
on prototype endpoints are intended to avoid unsolicited selection; coexistence with
WirePlumber has not yet been tested and those flags are not a proof of policy isolation.

Later custom routing must be explicitly configured. For ordinary single-target moves,
use WirePlumber's supported target mechanism. For fan-out, prefer routing a selected
application into an owned bus, leaving ordinary target selection with WirePlumber and
managing only downstream owned branches. Whether that supports all required capture
cases is an experiment. Do not implement a reconciliation loop that repeatedly deletes
WirePlumber's links. If an exclusive custom graph requires session-manager cooperation,
design a scoped opt-in hook rather than disabling desktop policy globally.

Profiles will store desired topology and matching rules, never volatile global IDs.
Stable project UUIDs identify owned devices; application matching needs application IDs,
binary/name, role and stream discriminators with explicit ambiguity handling. PID and
PipeWire object ID are runtime hints only. External user changes must have a defined
precedence before reconciliation is enabled.

## Configuration and reconciliation

`Configuration` separates saved intent from `Router`'s volatile link proxies. A schema-1
JSON document contains device UUID definitions, selectors for custom routes, and explicit
volume/mute settings for saved owned devices. QSaveFile commits before initiating new
persistent mutations; an unreadable/invalid/unsupported document is not repaired or overwritten.
A QLockFile prevents two backend processes from owning one configuration document. The
read-only `--snapshot` path never opens or restores configuration.

Application matching uses application ID or binary, optional advertised application name,
role and stream restore ID. Media titles, process IDs, global IDs and stream names are
excluded. Physical/external endpoints match node names with advertised hardware identity.
Ambiguity removes owned branches and leaves the rule waiting; it never guesses by label.
This intentionally does not handle simultaneous indistinguishable streams as a group.

Reconciliation observes only explicitly saved custom rules. It waits for ports, reapplies
channel and cycle validation, and remembers attempted endpoint/port generations. A failed
route on unchanged ports remains Failed rather than repeatedly overriding another client.
An external route can satisfy a rule without being adopted; removing it does not cause a
reconnect contest. Node/port recreation or an explicit user retry allows a new attempt.
Restoration is asynchronous and does not prevent first-buffer/default-route leakage.

Saved devices are recreated with the same UUID names after login or supervised backend
restart. The actual client-owned nodes still disappear when the backend exits. Deleting a
saved device removes its definition and referring rules; shutting down does not. Existing
VC1–VC3 are never converted. Mixer restoration applies only explicit project-device controls,
once per new node serial; external physical/application mixer policy remains WirePlumber's.

## Initial additive routing

`Router` owns client-lifetime `link-factory` proxies with `object.linger=false`, one per
mapped channel. Source/destination selection uses current object serials, never global
IDs alone. This first version does not change target metadata or remove links belonging
to WirePlumber/another client. It does not restore links that an external policy removes;
a broken route is reported Failed and its remaining channels are rolled back. Thus there
is no repeated contest with session policy. Desktop policy coexistence still needs an
interactive test before introducing restoration or exclusive moves.

The matrix supports Audio/Source nodes and nonvirtual Stream/Output/Audio nodes feeding
Audio/Sink nodes. Ports must expose positional float mono DSP audio. Exact channel sets
are mapped by position, and MONO may be duplicated into FL/FR. Stereo-to-mono and other
remixes are rejected until a supported processing topology is selected. In isolated tests,
PipeWire's native receiving port mixes two sources (0.125 + 0.25 = 0.375), and one source
fans out at unity to two sinks. This validates those receiving DSP ports on the installed
version, not every possible endpoint type or hardware clock combination.

Negotiation is asynchronous: all channel links must reach PAUSED/ACTIVE before Connected.
Errors or external removal produce Failed and clean up sibling links. Endpoint removal
cleans up runtime links; the saved rule waits for a uniquely matching endpoint. Client disconnect cleans up its links on SIGTERM and SIGKILL, without
removing an independently owned baseline route. Rapid create/remove is tested. Runtime
links remain client-owned, while a separate configuration layer stores desired routing
and restores it on endpoint/port recreation and backend startup. Multiple named profiles
remain unimplemented. No route nodes or links are created simply by opening the UI.

Cycle rejection includes visible graph edges, pending owned routes, link-group cable
paths, and monitor-capture target relationships. This is conservative for known internals;
it cannot prove arbitrary hidden processing or acoustic paths safe.

## Per-route processing is the architectural risk

A link carries audio and has no independent route gain/mute contract. Fan-out links
alone do not isolate branch volume, and fan-in behavior must be checked for each receiving
node/port. Device volume, source volume, stream volume, and route gain are separate controls.

Prototype PipeWire-provided loopback stream volume and filter-chain mixer/copy/gain
components. Do not commit to either topology or write custom sample processing yet.
Measure both idle and active operation against a direct-path baseline at the same rate,
quantum, channel map and workload. Start at 48 kHz and record the actual negotiated values.

The experiment must cover 1x1, 1xN, Nx1 and NxM routing; monotonic independent branch
gain; mute isolation; coherent-signal summing and clipping; channel remapping; stream
recreation; hotplug; multiple hardware clock domains; and Chromium/Discord/OBS capture.
Record end-to-end impulse latency, per-process and server CPU, xruns and negotiated
quantum. Repeat measurements and report variation before selecting a design or performance
threshold. Check that muting one route leaves another audible and that backend failure
has the documented behavior. Topological cycle detection must include cable relationships;
physical acoustic feedback cannot be ruled out by graph inspection alone.

## Validation boundaries

The current isolated test verifies registry discovery, D-Bus graph signals, input validation,
separate cable media classes, refusal to delete foreign objects, preservation of a sentinel
node, explicit removal, and cleanup after SIGTERM/SIGKILL. It does not establish uninterrupted
audio sessions, actual cable audio flow, per-route processing performance, or desktop policy
coexistence. Live read-only discovery is safe to use independently. No existing audio
configuration is changed by this code, and no routine operation requires a PipeWire restart.

The frontend is a native QML module containing a session D-Bus client. Snapshot responses
are validated, calls are asynchronous, graph notifications are coalesced, and generation
checks reject stale replies after service ownership changes. A backend-owned device
listing supplies authoritative deletion tokens, so UI reconnects do not infer ownership
from display names or adopt VC1–VC3. The GUI distinguishes owned devices from externally
managed virtual endpoints. Stream filters marked virtual are omitted from application lists.
Existing external playback links remain read only; recording input moves use WirePlumber target metadata. Node volume/mute are operational where Props and
permissions support them. Per-route gain and defaults remain unimplemented; additive custom route editing is operational.
Volume uses subscribed SPA Props and preserves channel balance. Calls address current
object serials rather than reusable global IDs; stale requests are rejected. Suspended
devices are displayed as Idle and retain their supported controls.

Expected ESTALE/ENOENT responses from observation proxies are nonfatal. Removed bindings
are retired until PipeWire acknowledges proxy removal, avoiding destruction requests for
resources whose bind never completed. Late callbacks cannot recreate removed snapshot
entries. Other object-operation errors are reported to the UI; core/connection failures
remain fatal. A deterministic isolated regression forces deletion before observation bind
and verifies subsequent node discovery and creation continue.

The installable widget bundles its client module with a local directory import in the
Plasma entry point; shared UI components use the module's registered URI. Native libraries
use relative loader paths. The built package needs no global QML import path modification.
Backend process lifetime remains independent of both GUI hosts.

## Runtime deployment

Source and build artifacts stay in the editable checkout. `tools/update.py` builds/tests
that checkout and copies the backend atomically into `~/.local/libexec/plasma-audio-manager`.
The enabled `plasma-audio-manager.service` uses that stable path with a home working directory,
so runtime startup does not depend on the development mount. It is tied to the graphical
session, uses D-Bus ownership for readiness, and restarts on failure after three seconds.
Device definitions, explicit owned-device mixer controls and custom routing rules are stored
atomically under the project's own XDG configuration directory. Multiple named profiles remain unimplemented.

Updates address the one observed D-Bus owner by PID/executable identity, never a process-name
kill. Handover/restart is refused while custom routes or saved-device connections are active,
or while unsaved temporary devices would be lost. Idle saved devices restore from UUID definitions. Unchanged
deployments do not restart the service. The service and updater never restart the audio
server, session manager or Plasma shell. Native widget updates may require a later shell
restart/login to load new native code; backend changes use only the backend restart.

## Primary references

- [PipeWire core API](https://docs.pipewire.org/group__pw__core.html)
- [PipeWire link factory](https://docs.pipewire.org/page_module_link_factory.html)
- [PipeWire loopback module](https://docs.pipewire.org/page_module_loopback.html)
- [PipeWire filter-chain](https://docs.pipewire.org/page_module_filter_chain.html)
- [WirePlumber linking policy](https://pipewire.pages.freedesktop.org/wireplumber/policies/linking.html)
- [WirePlumber restoration/settings](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html)
- [Plasma 6 packaging](https://develop.kde.org/docs/plasma/widget/porting_kf6/)

Some KDE widget C++ tutorial snippets still target KF5; this project uses Qt 6 QML module
tooling and Plasma 6 package metadata. It does not depend on plasma-pa's private QML
module. KDE runtime dependencies are limited to the frontend; the backend needs none.

## Recording target selection

Recording selection reuses Graph's registry connection and Configuration's store/identity
matcher. Graph subscribes to the writable `default` metadata object through libpipewire,
tracks `target.object`, and writes that key only for an explicitly selected recording
stream subject. Values are the target's current object serial; persisted selectors never
contain that serial. Metadata proxy retirement follows the same remove-id lifecycle as
node bindings. The read-only snapshot path does not write metadata.

Configuration stores optional schema-1 `recordingAssignments`, validates their application
and source selectors, and applies each assignment once per stream/source/metadata generation.
WirePlumber owns moving, remixing and removing the previous input links. The manager does
not delete foreign recording links or construct competing input links. Default selection
uses WirePlumber's supported `-1` target sentinel and forgets the project's assignment.
A later external move is observed without a reassertion loop. Source absence may allow
WirePlumber fallback; requested intent and observed sources remain distinct in the UI.

This does not change mute state. Two isolated real capture clients verify cable A's source
mute silences only A's signal, while B remains audible. This is source isolation, not
independent branch gain/mute; all clients of a muted source share its mute. No inference
is made about Android device names or Sober internals. Application-internal device lists,
permissions, Roblox voice processing and reception require user validation in the game.

References: [WirePlumber moving/restoration settings](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html),
[linking policy](https://pipewire.pages.freedesktop.org/wireplumber/policies/linking.html),
and the installed supported `linking/find-defined-target.lua` implementation for the default sentinel.
