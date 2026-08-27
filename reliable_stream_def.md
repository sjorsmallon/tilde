# Reliable stream — design

The gameplay event channel calls itself reliable and is not. The send is one
unacked `socket.send` per client per tick (`server_impl.cpp`, the
`outgoing.events.empty()` block); there is no sequence number, no ack and no
retransmit anywhere in the transport layer. A dropped datagram is a silently
lost event, and three features already depend on the guarantee (TODO.md,
"Reliable event channel").

Two stopgaps papered over it, and both said so at their site: the once-a-second
phase re-announce (`game_rules_state_t::last_phase_broadcast_tick` plus the
transition/re-send discriminator in `on_round_phase_changed`) and the 0.25s
`CmdChangeMap` retransmit (`change_map_resend_interval_seconds`). Both were
per-feature, timer-driven imitations of one transport-level mechanism. This
document is that mechanism, and both stopgaps are now gone.

## Status

**The mechanism has landed** (2026-08-27): `shared/network/reliable_stream.hpp`,
the two `Packet_Header` fields, `Message_Type::S2C_Reliable`, the per-slot
streams on `Server_Transport_Layer`, the client's receive/ack half, and the
three riders of §11. `reliable_stream_test` drives both sides by hand;
`server_loop_test` adds the round trip over a real socket pair, which is the
half a hand-driven test cannot reach (the header fields surviving the wire, the
client intercepting a block before reassembly, and the ack riding an ordinary
outbound datagram).

**§8 has landed** (2026-08-27): `round_phase` / `phase_end_tick` /
`round_number` ride `S2C_EntityPackage` every tick, `Round_Phase_Changed` is
purely the banner, and both `last_phase_broadcast_tick` + its heartbeat and the
transition/re-send discriminator are deleted. `S2C_ServerMessage` joined the
riders of §11.

**The map transfer has reliability of its own** (2026-08-27), which was the
blocker on §12: `C2S_TransferReceipt` is a fragment bitmap, `Outbound_Transfer`
holds a confirmed SET rather than a cursor, and a pass waits for a report before
running again. `shared/network/transfer_receipt.hpp` carries the reasoning for
why that is a different mechanism from this one rather than the same one reused;
`paced_transfer_test` and `server_loop_test` guard it.

**§12 has landed, and with it the C2S direction** (2026-08-27). Three changes in
one sitting, in this order, because each one removes a job the resend was still
doing:

1. **The C2S half of the stream.** `Reliable_Stream` was already symmetric; what
   was missing was the plumbing around it. `Message_Type::S2C_Reliable` became
   `Message_Type::Reliable` — a block is a transport parcel whose direction is
   already said by who sent it, so two ordinals would have been two names for
   one thing. `service_client_reliable_stream` is the client's mirror of
   `send_reliable_block`, called once per FRAME rather than per tick because the
   client has no tick loop while it is `Loading`, which is exactly when the
   request it carries matters. `C2S_RequestMapData` and `C2S_Command` moved onto
   it.

   The server needed a send choke point to match the client's
   `send_packet_to_server`, because it now has an ack to stamp on every outbound
   datagram and had no single place to stamp it:
   `send_packet_to_client(state, socket, slot, packet)`. That this matters is
   not theoretical — a `Loading` client receives no snapshots, so what carries
   the ack for its in-flight request is the map package's own fragments and the
   server's own blocks. Both go through the choke point. Only the rejected
   connect still reaches the socket directly, and it has no slot and therefore
   no stream to ack.

   Receiving is symmetric too: `poll_network` intercepts a block before
   reassembly and drains records after the datagram loop, into
   `deliver_client_message` — the SAME function the unreliable path files
   through, so nothing above the transport can tell which route a message took.

2. **The map hash rider**, replacing `C2S_MapLoaded` (see below).

3. **The resend, deleted** — `change_map_resend_interval_seconds`, the loop, and
   `client_slot_t::last_map_switch_send_tick`.

`server_loop_test::test_reliable_stream_round_trip_c2s` guards the direction over
a real socket pair: two records queued in one frame arrive in order and through
the ordinary delivery path, and an ordinary S2C datagram's header frees the
block.

One hole this opened and closed: `Client_Transport_Layer` survives leaving and
re-entering `Play_State`, so a second connection inherited the first's block
numbers and half-reassembled buckets. Silent in both directions — the stream
simply stops delivering. `reset_connection_scoped_state` is the client's
counterpart to `occupy_client_slot`, called from `reset_for_new_connection`.

## 1. No second socket

Neither Source nor Source 2 opens a TCP connection for reliable game data.
Everything rides the one UDP flow, with reliability implemented inside it —
Source 1 in `CNetChan` (a reliable byte stream cut into fragments, one per
subchannel, each with an ack bit that the peer echoes), Source 2 in
GameNetworkingSockets (a QUIC-shaped transport: one reliable byte stream, SACK
ranges, lanes). TCP appears only out of band: HTTP for master server and
workshop, RCON for the remote console.

A parallel TCP connection makes the important message arrive *later*, which is
the whole reason nobody does it. TCP's retransmit is timer-driven with a 200ms
RTO floor that doubles, and its send buffer bloats, so after loss the
must-not-be-missed message lands hundreds of milliseconds behind the UDP
snapshots describing the same world. Piggybacking a retransmit on traffic we
already send every tick recovers in one tick.

## 2. Recovery is sender-driven

The receiver never asks for anything. Its only utterance is "the newest block I
have is N" — which, because blocks arrive in order, also says it has every block
before N.

A receiver-requests-missing scheme is wrong here four ways. It cannot detect
tail loss — if the last block is dropped and nothing follows, the receiver has
no evidence it existed, so a timer is needed anyway. It doubles recovery
latency, since loss must be noticed, then a request must fly, then a response:
1.5 RTT against one tick for a retransmit that never stopped. It saves nothing,
because the sender must retain the block until acked regardless. And it is an
unsolicited request from a peer, which is an amplification surface.

## 3. One block outstanding

A **block** is a parcel of bytes cut from the outbound stream. It is cut by the
ack clock, not the tick clock: *whatever is pending the moment the stream last
became free*. On a LAN a block is roughly one tick's data; at 50ms RTT it is
three ticks' worth; under loss it is however much accumulated. The stream
self-batches as conditions worsen, which is the same thing Nagle does.

A block names nothing in the game's world — unlike `message_id`, which names an
application object the receiver hands over whole. A block is a transport parcel,
and a block boundary may fall in the middle of a message. It is to this stream
what a TCP segment is to a TCP connection.

**Exactly one block is outstanding at a time**, and that constraint is what buys
everything else: gaps become *unrepresentable*. There is no hole to request, no
receive window, no out-of-order buffering, no per-block ack state — ordering
costs nothing because it cannot be violated. Cutting one block per tick instead
would put several in flight, and all of that machinery comes back.

Throughput is one block per round trip: at 50ms that is ~20 blocks/sec of up to
~1150 bytes, which for deaths and phase changes is orders of magnitude more than
needed. Source's 8 subchannels exist to pipeline *bulk* reliable data; the map
transfer stays on its own paced path (`Outbound_Transfer`), so we need one.

### Nothing distinguishes one block from another

Block 7 and block 8 differ only in *when* they were sent. The number is a
tracking number on a parcel, not a label for what is inside — two blocks may
carry identical bytes. Queue `[Player_Died][Player_Died][Round_Phase_Changed]`
and a LAN cuts three blocks of one message each while a 50ms link cuts two, the
second holding the last two messages. The receiver's byte stream is
byte-for-byte identical either way and the app sees the same three messages in
the same order. The blocking is invisible above the transport, which is what
"a block carries no meaning" cashes out to.

What the number *is*: a compressed byte offset. TCP acks a 32-bit position in
the stream; here, because exactly one block is outstanding, the peer's report
can only be the offset before this block or the offset after it — two
possibilities, so one byte carries them (one bit would technically do, which is
the alternating-bit protocol, and is why Source's reliable state is one bit per
subchannel). `latest_reliable_block_received = 7` means precisely
`confirmed_bytes += block_length`.

The receiver never reassembles *by* block number — blocks arrive in order by
construction, so it appends. It uses the number for exactly one thing, spotting
a duplicate, and then discards it. It has a byte stream; it does not have
blocks. **Blocks are the sender's units**, and they exist so the sender can
answer one question about itself: can I let go of these bytes yet?

Coalescing several messages into one block is not a nicety, it is what keeps a
burst at one round trip instead of one per message. But it is the single way the
blocking leaks upward: messages sharing a block are dispatched on the same tick,
and which messages share one depends on RTT and loss. **Rely on order, never on
grouping** — a handler that assumes co-arrival works on a LAN and breaks on a
connection.

## 4. The stream is bytes, framed

The outbound side is a byte stream, not a message queue, so a message larger
than one datagram is representable — it simply takes several blocks. Records are
framed inside the stream:

```
[message_type: u8][length: u32][payload: length bytes]
```

`u32` rather than `u16` because a record's length is not bounded by the
datagram, and a cap that is hit exactly once will be hit at the worst time.
Five bytes per record, and records are rare.

This is the same framing the transport already performs — `Packet_Header`
carries `message_type` and `payload_size` — moved inside the payload and
concatenated rather than sent one per datagram. Nothing about message cleanness
is lost; a bit-packed payload is padded to a byte boundary before it is
appended.

The buffer is **self-describing**, so there is deliberately no index of
`{type, offset, length}` beside it. That would be a second copy of information
already in the bytes, and a second copy that can disagree is the failure
`body_yaw`, `held_snapshot_tick` and `last_broadcast_cvars` each already paid
for. `sv_reliable_debug` walks the records instead.

## 5. Wire format

Two fields in `Packet_Header`, carried by every datagram:

```c++
uint8 reliable_block_number;          // 0 = no block attached
uint8 latest_reliable_block_received; // 0 = nothing received yet
```

They look like a matched pair and are not: the first describes **my** outbound
stream, the second describes **yours**. They share a datagram because it is
free.

`reliable_block_number` does not increment per datagram — it increments per
block, so five retransmissions of block 7 all carry 7. That is what makes it
"which block is this", not "which packet is this".
`latest_reliable_block_received` is a high-water mark, and reporting 7 implies
1..6 as well, since blocks arrive in order. That is what makes a lost or
reordered ack harmless: the next datagram carries a newer one.

Blocks are numbered 1..255 and wrap skipping 0, so 0 means "no block attached"
with no extra flag — the same convention as server ticks starting at 1 because 0
is `Snapshot_History`'s empty slot.

The two fields **consume the existing padding exactly**: the header is
`1+1+1+1+2 = 6` bytes today, padded to `PACKET_PAYLOAD_OFFSET_IN_BYTES = 8` by
`payload_alignment_padding`. Two `uint8`s replace that padding, the header
becomes 8 bytes with no padding, and the offset is unchanged. The existing
`static_assert` on `offsetof(Packet, buffer)` guards it.

The block itself rides a message of its own, `Message_Type::S2C_Reliable`, sent
every tick while unconfirmed. It is **never gated on `map_ready`** —
`CmdChangeMap` is one of the messages riding it.

### Cadence: three different clocks

Only one of the three is opportunistic, and conflating them is easy:

- **Cutting** a block is opportunistic — whenever the stream is free and bytes
  are pending (§3).
- **Sending** it is a fixed cadence — once per tick, unconditionally, until
  confirmed. The send path cannot tell a first transmission from a fortieth.
- **Freeing** is event-driven — on the ack, and nothing else (§7).

Sending every tick is deliberately wasteful: at 50ms RTT roughly three copies go
out before an ack could physically arrive, so two are redundant by construction.
At these block sizes that is the right trade — a 30-byte block sent three times
costs 108 bytes instead of 36, a few times a second, and buys 16ms recovery with
no RTT estimate, no retransmit timer and no timer state to get wrong.

**The condition that would change it:** blocks approaching the datagram budget.
At ~1150 bytes, every-tick retransmit costs ~3.4KB to deliver one block and the
resend should be paced off measured RTT instead — which needs the packet sequence
number §11 defers. But a block that large means bulk data, and bulk belongs on
`Outbound_Transfer`. The cadence and the scope decision hold each other up.

Giving the block its own datagram rather than prepending it to existing payloads
costs one ~36-byte datagram only while a block is in flight, which is a handful
of ticks at a time. In exchange every existing send and receive path is
untouched and the stream is one message type testable in isolation.
Piggybacking would only pay off on a stream that is busy, and this one is idle
in the large majority of ticks.

## 6. The type

```c++
struct Reliable_Stream
{
  // Framed records, back to back. Self-describing; see §4.
  std::vector<uint8> outbound;
  size_t confirmed_bytes = 0;
  size_t block_length    = 0; // 0 == free
  uint8  block_number    = 0;

  std::vector<uint8> inbound;
  uint8  received_through = 0;
};
```

The block in flight is `Span{outbound.data() + confirmed_bytes, block_length}` —
a range of the same buffer, not separate storage, so there is no second copy to
disagree with the queue. Offsets rather than a stored `Span` because
`std::vector` reallocates on append; this is the rule `ui_node_id_t` already
enforces for the same reason.

## 7. The two loops

Free, in `poll_network`, per incoming datagram:

```c++
if (header.latest_reliable_block_received == stream.block_number)
{
    stream.confirmed_bytes += stream.block_length;
    stream.block_length = 0;
}
```

Equality, not `>=`. With one block outstanding the peer's value is only ever N-1
or N, and equality is also what makes a stale duplicate ack harmless: it names a
block already confirmed, so it matches nothing and frees nothing.

Cut and send, in the send phase of the same tick:

```c++
if (stream.block_length == 0 && stream.confirmed_bytes < stream.outbound.size())
{
    stream.block_length = std::min(stream.outbound.size() - stream.confirmed_bytes, budget);
    stream.block_number = next_block_number(stream);
}

if (stream.block_length != 0)
    send_reliable_datagram(stream);
```

The send path cannot tell a first transmission from a fortieth. Same code, same
bytes — retransmission is not a recovery path that is entered, it is what "still
unconfirmed" looks like at send time.

Receive:

```c++
if (header.reliable_block_number == stream.received_through + 1)
{
    stream.inbound.insert(stream.inbound.end(), payload.begin(), payload.end());
    stream.received_through = header.reliable_block_number;
    // then parse and dispatch complete records off the front
}
```

A block numbered at or below `received_through` is a **duplicate** — the sender
had not yet seen our ack — and must be discarded, not re-delivered. This is the
common case under loss, and it is the one bug that matters: a re-delivered
`Player_Died` is a second kill-feed row. Exactly-once delivery is the stream's
job, not the consumer's, which is what lets the handlers stop caring.

Reclaim, once drained:

```c++
if (stream.confirmed_bytes == stream.outbound.size())
{
    stream.outbound.clear(); // keeps capacity, like event_stream_t::reset
    stream.confirmed_bytes = 0;
}
```

No compaction, no ring buffer, no rebasing. The buffer only grows while the
stream is continuously busy, and a stream that stays busy is the overflow case
below.

## 8. It cannot get stuck while the connection is alive

The ack rides in the `Packet_Header` of every datagram the peer sends, and a
client sends `C2S_ClientInputBatch` every tick. So if any traffic flows at all,
acks flow. A permanently stuck stream means the peer has gone silent, which
`sv_timeout` already handles. There is no stream timeout, no retry counter and
no give-up path, deliberately.

Overflow is the one failure that is ours: an outbound buffer past a cap means a
peer that is not confirming while we keep queueing. That is a **loud
disconnect**, naming the slot and the buffer size — never a silent drop of the
oldest records.

### The snapshot channel outruns this, by design

The two channels are on different clocks: a snapshot is one datagram with no
waiting, this stream delivers after at least one round trip. So a snapshot
describing a world state can arrive before the event that caused it. That skew
already exists today — both are separate datagrams with undefined relative
order — but it is unbounded and silent, and this design makes it bounded and
self-healing at roughly `RTT / (1 - loss)`.

The rule it forces: **an event must never be the only source of truth for
anything the simulation reads.** This is the UI layer's rule at a different
altitude — continuous values are polled from the truth, discrete occurrences are
pushed into a model with a lifetime.

Two of the three current riders already satisfy it. `CmdChangeMap` cannot be
outrun because snapshots are withheld until `map_ready`. `Player_Died` is an
occurrence with a lifetime, so a late kill-feed row is a late row and nothing
else. **`Round_Phase_Changed` violates it**: the client predicts against the
phase (`shared/round_phase_rules.hpp`), so a phase arriving a round trip late
means predicting a freeze the server is not simulating.

The fix is not to synchronise the channels. Replicate `round_phase`,
`phase_end_tick` and `round_number` as **state on `S2C_EntityPackage`** — it is
per-tick server state, not an entity — and let `Round_Phase_Changed` become
purely the banner occurrence. The snapshot then cannot outrun the phase because
the phase is in the snapshot, the prediction gate reads state that self-heals
every tick, and the transition discriminator in `on_round_phase_changed` dies
for a better reason than the one in §12: there is nothing left to discriminate.

## 9. Where it lives, and what resets it

`Server_Transport_Layer`, one per slot, beside `partial_packets`:

```c++
std::array<Reliable_Stream, sv_max_client_count> reliable_streams{};
```

That is the right stratum, and `Outbound_Transfer` is the sibling that proves
it — "pure transport: it knows a byte range and a rate, never what the bytes
mean". This stream knows a byte range and an ack rule, and has no opinion about
whether it carries a death or a map switch.

It **survives** `reset_state_in_preparation_for_new_map_load` (it lives under
`transport_layer`, in the nothing-resets-these group — the map switch is a
message riding it) and it **must be cleared by** `reset_client_slot`, or the
next client in that slot inherits a block number and a half-reassembled inbound
buffer from its predecessor. `server_context_test` asserts both halves.

## 10. Naming

`channel` was rejected: it is the `.def` keyword (`Effect :: channel`), and "the
event channel goes on the reliable channel" is a sentence that will be said
constantly and is confusing every time. `lane` was rejected because the word
means *one of several* and there is one — it becomes correct the day a second
ordering domain exists (bulk versus gameplay, which is what GNS lanes separate),
and adopting it before then names a design that has not been built.
`Reliable_Stream` is what TCP, QUIC and Source (`NET_STREAM_RELIABLE`) all call
this object, and it is singular by nature.

## 11. Scope

**In, S2C:** `S2C_GameEventBatch`, `CmdChangeMap`, `S2C_CvarValues` — which
closes the documented lost-update hole in the mirrored-cvar broadcast ("that is
the whole lost-update story; there is no ack") — and `S2C_ServerMessage`.

**In, C2S:** `C2S_RequestMapData` and `C2S_Command`. Both are one-shot requests
whose loss nothing else recovers from, which is the test for this stream:
`C2S_ClientInputBatch` and `C2S_TransferReceipt` stay unreliable because both are
continuously restated, so the next one corrects a lost one.

This direction was shipped second, deliberately — one direction was half the work
and the map handshake was the only thing that needed the other half. It stopped
being optional when the `map_ready` rider (§12) took the last non-C2S job off the
`CmdChangeMap` resend and left it standing for `C2S_RequestMapData` alone.

**Out, deliberately:**

- **A packet sequence number.** Source has one for RTT, loss statistics and
  rejecting stale unreliable payloads. Only the third is a correctness item, and
  it is already solved a layer up: `try_decode_snapshot` drops any package whose
  `server_tick` is not newer than what is held. Add it when the net graph wants
  ping and loss, as a diagnostic, not as a dependency of the guarantee.
- **Multiple blocks in flight.** Only bulk data justifies the receive window it
  costs, and bulk is `Outbound_Transfer`'s.
- **The effect channel.** Effects stay unreliable — that is the split
  `events.def` already argues, and head-of-line blocking is the reason it must
  hold.

## 12. What lands, and what dies

Landing this **deletes**, and the deletion is the acceptance test:

- ~~`game_rules_state_t::last_phase_broadcast_tick` and the heartbeat re-send in
  `update_game_rules`~~ — DONE
- ~~the transition/re-send discriminator in `on_round_phase_changed`~~ — DONE
- ~~`change_map_resend_interval_seconds` and the `CmdChangeMap` retransmit in
  `server_impl.cpp`~~ — DONE, along with
  `client_slot_t::last_map_switch_send_tick`, `Message_Type::C2S_MapLoaded`,
  `map_loaded_message_t` and its codec, and `ServerInbox::map_loaded_acks`

All three were marked as stopgaps at their sites.

**The third one has a prerequisite, and skipping it hangs a client.** That
resend is gated on `!map_ready`, so it heals a loss in *both* directions: a
dropped `CmdChangeMap` going out, and a dropped `C2S_MapLoaded` coming back. The
reliable stream only covers the first. Delete the resend without covering the
second and a lost `C2S_MapLoaded` leaves `map_ready` false forever, which
withholds snapshots forever.

The prerequisite is not a C2S reliable stream. `map_ready` is client *state* the
server needs, not an occurrence, so it becomes a **rider on `C2S_ClientInput`**
exactly as `held_snapshot_tick` already is: the client reports the map content
hash it currently holds, every tick, and the server sets `map_ready` by
comparing it to its own. There is no ack to lose — only a value that is
continuously true — so it self-heals in one tick with no retransmit anywhere,
and `C2S_MapLoaded` disappears as a message type.

Two things fell out of it being derived rather than announced. The server's two
manual writes are gone — the optimistic `map_ready = true` at accept (which sent
snapshots to a client that turned out to need a download) and the
`map_ready = false` when a transfer starts (which the map-load reset already
covers). And the client's two `Loading -> Connected` edges now send *nothing*:
entering `Connected` starts the input flow, and the hash rides it.

Only the client's **newest** input in the batch answers, which is the one place
this differs from its neighbour `held_snapshot_tick`. That one is a high-water
mark over a number the client only advances, so every entry may contribute. This
one is a comparison against a value that legitimately goes back to false when the
map changes — and the batch is the client's unacked *tail*, which survives a map
switch on the client and therefore straddles one. Reading every entry flips the
answer twice per tick for as long as a pre-switch input is still unacked.

Same rule as the phase fix in §8, applied in the other direction: **state that
gates behavior is replicated as state, never delivered as an event.** That the
rule caught a hang on each side of the connection is the argument for it.

`reliable_stream_test` guards: in-order delivery; duplicate suppression on the
lost-ack path; a record straddling two blocks; the cut respecting the datagram
budget; reclaim on drain; and overflow reporting rather than dropping.

### RESOLVED: the resend healed a THIRD direction this section did not name

The `!map_ready` gate on the `CmdChangeMap` resend also covers a dropped
fragment of the **map package itself**. `S2C_MapData` rides `Outbound_Transfer`,
which is paced but unreliable and has no retransmit of its own, so today the
only thing that recovers a lost fragment is the next resend arriving, the client
still being in `Loading`, and it re-requesting the package. Neither the reliable
stream nor the `map_ready` rider covers that: the rider makes the server's
*knowledge* self-healing, but nothing re-drives the transfer.

So deleting the resend outright hung a client whose download lost a fragment.

**Resolved by giving the transfer an ack of its own**, which was the third of the
three candidates and the only one that is not another timer. It is deliberately
NOT this stream reused: see the table in `transfer_receipt.hpp`. The C2S
direction — a lost `C2S_RequestMapData` — is closed by the C2S half of this
stream, not a timer either.

So the resend was healing three different losses, one per direction of the
handshake plus the transfer, and each is now healed by the mechanism that owns
it: the stream S2C, the receipt bitmap for the package, the stream C2S for the
request, and nothing at all for the client's readiness, which is state and cannot
be lost.

Second, smaller, and now moot: while the resend and the reliable stream coexisted
it queued **guaranteed** duplicates rather than lossy ones, so a client that had
finished downloading but not yet applied the map re-requested the package once
per resend interval, reliably, instead of sometimes — restarting the transfer
each time, since `begin_paced_transfer` replaces whatever a slot is sending. That
was an argument for doing §12 sooner rather than later, and it is what the
client's "already downloading exactly this map" guard now makes unrepresentable.
