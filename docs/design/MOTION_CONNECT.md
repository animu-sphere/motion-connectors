# `motion_connect`

> Status: **accepted**, 2026-10-04. §2–§4 become **binding** when
> `motion_connect bridge` lands with the tests §6 names. §5 is reserved until
> `CLI-O1` is decided. The capability matrix says what is implemented.
>
> This document owns what the shared-contract CLI does beyond reading one
> connector: which connectors it opens, what `bridge` forwards and what it
> keeps, and what `record` will capture. It also records the producer side of
> `FW-O2`: connector state and diagnostics do not cross the wire (§4). How a
> frame is spelled is [FRAME_WIRE_FORMAT.md](FRAME_WIRE_FORMAT.md)'s, how it
> travels is [WEBSOCKET_CONNECTOR.md](WEBSOCKET_CONNECTOR.md)'s, and what it
> means is [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md)'s. None is restated
> here. On `bridge` and `record` this document wins over the design policy's
> §36 sketch. Section numbers are stable; open questions are `CLI-O<n>` and
> never reused.

---

## 1. Scope

- **In:** the connectors `motion_connect` can open (§2); `bridge`, which
  forwards one connector's frames to WebSocket peers (§3); why upstream state
  and diagnostics stay at the bridge (§4); what `record` must be, once it is
  designed (§5); the tests that make §2–§4 binding (§6).
- **Out:** the per-connector record tools and their options (WS-O3,
  [WORKSPACE §1.3](../architecture/WORKSPACE.md#13-tools-examples-bindings-and-data));
  the message format; WebSocket roles, framing and refusals; a binary encoding
  (`FW-O1`); TLS (`WSC-O1`).

`list`, `dump` and `inspect` shipped in v0.1.0, and their code is their
reference. This document changes them only by adding a source (§2).

## 2. Sources

`--source` takes `vmc`, `mocopi`, `vrchat-osc` and, from v0.2.0,
`websocket`. Every command that takes `--source` takes all four:

- **`list`** gains a `websocket` row. Its profile line reads
  `profile: (the sender's)`, because the profile is each frame's
  ([WEBSOCKET §7](WEBSOCKET_CONNECTOR.md#7-frame-assembly)). Its capabilities
  are the ones the wire can carry.
- **`dump --source websocket`** opens the receiving listen role through
  `IMotionConnector::Open(ConnectorConfig)`, on `--listen` and `--port`, as
  the UDP sources open theirs. The connector accepts any profile. **`--port`
  is required**, because no port is the protocol's
  ([WEBSOCKET §3](WEBSOCKET_CONNECTOR.md#3-roles-and-directions)). Port 0
  lets the OS choose, and `dump` prints the bound endpoint as
  `listen: <address>:<port>` before the first frame, so a test can find it.
- **`inspect --source websocket`** reads a `!websocket-packet-capture` and
  replays each message through `PushMessage`, with its peer.

The header line names the profile, as it does for the UDP sources. For
`websocket` it reads `source: websocket`, and each frame line carries its
own `profile=`.

The WebSocket source adds `motionConnectorWebSocket` to the CLI's links, and
through it `motionConnectorWire`. That is WORKSPACE §2.1's edge from a tool
to the connectors it names. `motion_connect` is built when all four
connectors are on.

## 3. `bridge`

```text
source connector ──Poll──→ MotionFrame ──Send──→ WebSocketFrameSender ──→ peers
```

`bridge` opens one source connector and one `WebSocketFrameSender`. Each
tick, it polls every frame the connector has, hands each to `Send`, and then
calls `Service`. Nothing in between reads or changes a frame. The design
policy's two limits hold: `bridge` forwards a `MotionFrame` and never
retargets it, and it records nothing.

### 3.1 What it forwards

- **Each frame as the source connector delivered it**, `frameNumber` and
  `receiveTimestamp` included
  ([WEBSOCKET §7.1](WEBSOCKET_CONNECTOR.md#71-sending)). The receiving
  connector rewrites both. The source connector numbers from 1 after its
  `Open`, so a receiver reports `WEBSOCKET_SOURCE_RESTARTED` only when the
  bridge itself restarts.
- **Nothing it did not poll.** `Poll` returns no frame while the source is
  `Connecting`, `Error` or `Disconnected`
  ([CONNECTOR_CONTRACT §5](CONNECTOR_CONTRACT.md#5-state)). `bridge` therefore
  sends nothing then. It sends no keep-alive and no placeholder frame, and it
  does not repeat the last frame.
- **A frame the encoder refuses goes to nobody.** Its `WIRE_*` code is
  reported (§3.4), and the receiver sees the missing number as
  `WEBSOCKET_FRAME_GAP`. A refusal here means a connector delivered a frame
  the contract forbids, such as a non-finite value, so it is a defect to
  report, not an event to hide.
- **The peers stay connected through upstream trouble.** A source that goes
  quiet or into `Error` closes no peer. A receiving peer sees a quiet link,
  and when the source recovers, frames resume on the same connection.

### 3.2 Command line

```text
motion_connect bridge --source <vmc|mocopi|vrchat-osc|websocket>
                      [--listen ADDR] [--port N] [--capture PATH]
                      [--output websocket]
                      (--ws-listen ADDR | --ws-connect ADDR) --ws-port N
                      [--ws-path PATH] [--allow-origin ORIGIN]...
                      [--max-frames N] [--duration S]
```

| Option | Meaning | Default |
| --- | --- | --- |
| `--source`, `--listen`, `--port` | the source connector and where it listens, as for `dump` (§2) | `--listen 127.0.0.1`, the source's port |
| `--capture PATH` | replay a capture instead of listening (§3.3); `--listen` and `--port` are then refused | live |
| `--output websocket` | where frames go; the only value in v0.2.0 | `websocket` |
| `--ws-listen ADDR` | serve peers that connect to `ADDR` | `127.0.0.1` |
| `--ws-connect ADDR` | connect to a listening peer at `ADDR`, a numeric address; refused with `--ws-listen` | — |
| `--ws-port N` | the WebSocket port; **required**. Port 0 is accepted only with `--ws-listen` | — |
| `--ws-path PATH` | the request target | `/` |
| `--allow-origin ORIGIN` | an origin a listening bridge lets in; repeatable | none |
| `--max-frames N`, `--duration S` | stop after N frames polled, or S seconds; 0 is unlimited | 0 |

Every other `WebSocketConfig` value stays at its default: 8 peers, 64 queued
messages each, the 65 507-byte bound and the reconnect timing. A flag is
added when a session needs a different value, and not before.

**The defaults keep the performer's motion on the machine.** The WebSocket
side binds loopback, and no origin is allowed. A page served from an allowed
origin can read everything the bridge sends. So `--allow-origin` names the
origin that should have it, and no wildcard exists. Serving another
interface is `--ws-listen` with that address, and across a network the
operator does not trust it waits for TLS (`WSC-O1`).

### 3.3 Replaying a capture

With `--capture`, the source connector reads a saved capture through its
`PushDatagram` or `PushMessage` path instead of a socket, as `inspect` does.
The frames are the ones `inspect` delivers from that capture, and `bridge`
forwards them the same way it forwards live frames. This puts a recorded
session in front of a browser module or a native runtime without a device,
and it gives the CLI an end-to-end test (§6).

- **Replay waits for a peer.** It starts when the first peer completes its
  handshake, because a frame sent with no peer in session goes to nobody.
- **Replay keeps the recorded pacing.** A record is pushed when the time since
  replay started reaches its receive time minus the first record's. A
  receiver therefore sees the session's rate, and a peer's 64-message queue
  does not overflow on a replay faster than the session was.
- **Replay ends with the capture.** The connector's replay flush runs
  (VMC's `Flush`, VRChat OSC's `Flush`), and `bridge` keeps calling `Service`
  until every peer's queue is empty or one second has passed. It then closes,
  and exits 0. `--max-frames` and `--duration` can stop it sooner.

### 3.4 What it prints, and when it stops

Standard output carries the session:

```text
source: vmc.v1
output: ws://127.0.0.1:8765/
peer: + 127.0.0.1:51022
state: connected
state: degraded
peer: - 127.0.0.1:51022
frames: 1832
refused: 0
```

- `source:` is the profile, as for `dump`, and `output:` is the sender's
  endpoint (the bound one when listening, so port 0 shows its port).
- `peer: +` and `peer: -` mark a peer that completed its handshake and one
  that went away.
- `state:` is printed when the **source** connector's state changes. It is
  the state `bridge` keeps (§4).
- `frames:` counts frames polled from the source, and `refused:` counts the
  ones the encoder or the message bound refused. No line is printed per frame:
  `dump` exists for that, and a 60 Hz bridge would flood its own terminal.

Standard error carries every diagnostic, the source connector's and the
sender's, in the shared one-line form, as it is raised:

```text
[WEBSOCKET_PEER_REFUSED] info recoverable source=127.0.0.1:51040 subject=127.0.0.1:51040: ...
```

`bridge` raises no code of its own. Every event it reports is already a
connector's, the sender's or the codec's.

It stops on `SIGINT`, `--max-frames`, `--duration`, or the end of a capture.
On stopping, it closes the sender, with close 1001 to each peer, closes the
source, prints the counts, and exits 0. It exits 1 when the source or the
sender cannot open (a bind failure, an unreadable capture), and 2 on a usage
error, as the other commands do.

## 4. State and diagnostics stay at the bridge

`FW-O2` asked whether connector state and diagnostics cross the wire, so that
a bridged consumer sees a `Degraded` source as the sender did. **They do not,
in version 1.** Decided on 2026-10-04 with `bridge`, the producer that has an
upstream state to forward:

- **A receiver's state is its link's.** `WebSocketConnector`'s
  `ConnectorState` is the connection's
  ([WEBSOCKET §6](WEBSOCKET_CONNECTOR.md#6-the-session-and-its-state)), as
  every connector's is the connection's and not the tracking's
  ([CONNECTOR_CONTRACT §5](CONNECTOR_CONTRACT.md#5-state)). A forwarded
  upstream state would need a second state in `IMotionConnector`, or would
  make one state mean two things.
- **A diagnostic code belongs to the layer that raised it.** A receiver that
  re-raised `VMC_INCOMPLETE_FRAME` would claim a VMC decoder it does not have.
  The codes that do pass through (`WIRE_*` through the WebSocket connector)
  pass through inside one process, from the layer that raised them
  ([DIAGNOSTICS §1](../reference/DIAGNOSTICS.md#1-the-record)).
- **What a consumer must react to already arrives.** A source that delivers
  nothing reaches the receiver as silence, which its silence timeout reports
  as `Degraded` with `WEBSOCKET_SOURCE_TIMEOUT`. A source that delivers sparse
  frames reaches it as sparse frames. An absent joint is an absent key
  ([FRAME_WIRE_FORMAT §4.1](FRAME_WIRE_FORMAT.md#41-absence-is-an-absent-key)),
  so the frame says what is missing.
- **The operator sees the rest at the bridge** (§3.4). The bridge is where the
  upstream connector runs, so it is where its diagnostics mean something.
- **Nothing a later version needs is closed off.** A status message would be
  negotiated as a second subprotocol beside `openstrata.motion.frame.v1`, as a
  binary encoding would be (WEBSOCKET §4.1). A version 1 peer would then
  never receive one.

A consumer that needs the upstream state, `usd-avatar-runtime` for example,
opens a new question with its evidence. `FW-O2` is not reused for it.

## 5. `record`

Reserved. `record` captures what a connector received, below decoding, in
that connector's packet-capture format
([CONNECTOR_CONTRACT §12](CONNECTOR_CONTRACT.md#12-raw-capture)), through the
shared connector contract rather than through the per-connector record tools,
which remain (WS-O3). It takes no connector's own options, and it writes no
session report and no trace.

For `--source websocket` the capture already exists:
`WebSocketConnector::StartCapture` records each message in
`!websocket-packet-capture`
([WEBSOCKET §9](WEBSOCKET_CONNECTOR.md#9-raw-capture)). The three UDP
connectors expose no capture. Their record tools capture below the connector,
from the transport's `UdpReceiver`. How `record` reaches their datagrams is
`CLI-O1`, and this section is written when that is decided.

## 6. Tests

§2–§4 become binding with these tests, which bind loopback sockets and need no
device:

| Test | What |
| --- | --- |
| `motion_connect_list` | the inventory, with the `websocket` row |
| `motion_connect_inspect_websocket` | a WebSocket message capture replayed through `PushMessage` |
| `motion_connect_dump_websocket` | `dump --source websocket --port 0` prints its endpoint, opens and stops |
| `motion_connect_bridge_vmc`, `_mocopi`, `_vrchat_osc` | `dump --source websocket` listens, and `bridge --capture … --ws-connect` replays that connector's capture into it. `dump` receives exactly the frames `inspect` delivers from the same capture, with the source's profile |
| `motion_connect_bridge_listen` | `bridge --ws-listen --ws-port 0` serves a client the test writes on the Python standard library. The client receives `openstrata.motion.frame/v1` messages numbered 1 to N. A client sending an `Origin` that is not allowed gets HTTP 403 |
| `motion_connect_arguments` | the bridge refusals: no `--ws-port`, both roles, `--ws-connect` with port 0, an unknown `--output`, `--capture` with `--listen` |

The test client is independent of `motionConnectorWebSocket` on purpose.
`bridge` to `dump` checks that the tree agrees with itself, and the client
checks that the tree's bytes are what another implementation reads.
`workspace_installed_consumer` runs the installed `list` with the new row.

## 7. Open questions

`FW-O2` was decided on 2026-10-04 (§4).

| Id | Question | Resolve by |
| --- | --- | --- |
| CLI-O1 | How `record` captures a UDP source through the shared contract. Options: the three UDP connectors expose `StartCapture` / `GetCapture`, as `WebSocketConnector` does; or `record` opens the transport's `UdpReceiver` itself, as the record tools do, which bypasses the connector it names | `motion_connect record` (v0.2.0) |
