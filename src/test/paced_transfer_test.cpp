// The sender-side flow control, the reassembly bucket's lifetime, and recovery.
//
// The first two exist because UDP gives us neither. A bulk message handed to the
// socket in one loop overruns the receiver's kernel queue -- loss that happens
// before its first recvfrom, which no receive-side change can recover -- and a
// message that loses a fragment leaves a bucket that never completes and eats
// whoever reuses its wrapped message_id.
//
// The third is RECOVERY, which pacing alone never gave: the receiver reports
// which fragments it holds and the sender re-sends exactly the rest. What is
// worth guarding there is that the repair is PROPORTIONAL TO THE LOSS -- the
// transfer's state is a set, not a cursor, and go-back-N would re-send
// everything after a gap -- and that a pass never runs a second time off its own
// initiative, because the retransmit rate has to be the receipt rate rather than
// a timer somebody picked.
//
// Undo any of the three and a map download stops converging.

#include "shared/network/packet.hpp"
#include "shared/network/server_transport_layer.hpp"
#include "shared/network/transfer_receipt.hpp"

#include <cassert>
#include <cstdio>
#include <thread>

using namespace network;

static std::vector<uint8> make_payload(size_t bytes)
{
  std::vector<uint8> payload(bytes);
  for (size_t i = 0; i < bytes; ++i)
    payload[i] = static_cast<uint8>(i * 31 + 7);
  return payload;
}

// Counts the datagrams a tick of service_paced_transfers actually put on the
// wire. A real socket pair, because the thing under test is the send loop, and a
// hand-walked cursor would only be testing the walk.
struct wire_t
{
  Udp_Socket sender;
  Udp_Socket receiver;
  Address receiver_address;

  wire_t(uint16 sender_port, uint16 receiver_port)
  {
    if (!sender.open(sender_port) || !receiver.open(receiver_port))
    {
      printf("  FAILED to open the socket pair\n");
      exit(1);
    }
    receiver_address = Address(127, 0, 0, 1, receiver_port);
  }

  ~wire_t()
  {
    sender.close();
    receiver.close();
  }

  // Drains everything queued and reports which fragment indices arrived.
  std::vector<uint16> drain()
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    std::vector<uint16> indices;
    Packet packet;
    Address from;
    while (receiver.receive(packet, from))
      indices.push_back(packet.header.fragment_index);
    return indices;
  }
};

// A transfer is handed out at the requested rate and no faster, and every
// fragment goes out exactly once per pass. The whole point is the RATE, so
// "it all arrived eventually" is not what is being asserted here.
static void test_transfer_is_paced()
{
  // 91xx, not 90xx: server_loop_test owns 9001-9008 and ctest runs the two in
  // parallel. UDP sockets here are opened with SO_REUSEADDR, so a shared port
  // binds in BOTH processes and the datagram is delivered to whichever -- a
  // failure that looks exactly like a lost packet in whichever test lost.
  wire_t wire(9105, 9106);

  Server_Transport_Layer transport;
  transport.slot_occupied[0] = true;
  transport.addresses[0] = wire.receiver_address;

  const std::vector<uint8> payload = make_payload(40 * MAX_PAYLOAD_SIZE_IN_BYTES);
  begin_paced_transfer(transport, 0, payload,
                       static_cast<uint8>(Message_Type::S2C_MapData));

  const size_t fragment_count = transport.outbound_transfers[0].fragments.size();
  assert(fragment_count == 40 && "payload should fragment into 40 packets");

  // Every fragment of one message shares an id -- that is what lets the receiver
  // group them, it is why one message never spans two buckets, and it is the
  // identity a receipt names.
  const uint8 message_id = transport.outbound_transfers[0].message_id;
  for (const Packet &fragment : transport.outbound_transfers[0].fragments)
    assert(fragment.header.message_id == message_id);

  size_t ticks = 0;
  size_t delivered = 0;
  while (!transport.outbound_transfers[0].awaiting_receipt)
  {
    service_paced_transfers(transport, wire.sender, 8);
    const size_t sent = wire.drain().size();
    assert(sent <= 8 && "a tick must never exceed the rate");
    delivered += sent;
    ++ticks;
    assert(ticks <= 5 && "40 fragments at 8/tick must cover the range in 5 ticks");
  }

  assert(ticks == 5);
  assert(delivered == fragment_count && "one pass sends each fragment once");

  printf("  paced transfer: %zu fragments in %zu ticks at 8/tick\n",
         fragment_count, ticks);
}

// The sender does NOT start a second pass on its own. Without this, a 1700
// fragment map re-sends itself in full before the receiver could physically have
// reported a single fragment of it.
static void test_a_pass_waits_for_a_receipt()
{
  wire_t wire(9107, 9108);

  Server_Transport_Layer transport;
  transport.slot_occupied[0] = true;
  transport.addresses[0] = wire.receiver_address;

  begin_paced_transfer(transport, 0, make_payload(4 * MAX_PAYLOAD_SIZE_IN_BYTES),
                       static_cast<uint8>(Message_Type::S2C_MapData));

  service_paced_transfers(transport, wire.sender, 8);
  assert(wire.drain().size() == 4);
  assert(transport.outbound_transfers[0].awaiting_receipt);

  // Ten ticks with nothing coming back: not one byte more goes out.
  for (int tick = 0; tick < 10; ++tick)
    service_paced_transfers(transport, wire.sender, 8);
  assert(wire.drain().empty() && "a silent receiver must not be re-sent to");

  printf("  a completed pass waits for a receipt before the next one\n");
}

// The payoff: a receipt naming one gap costs one fragment to repair, not a
// restart. This is the entire reason the transfer's state is a SET.
static void test_only_the_missing_fragments_are_resent()
{
  wire_t wire(9109, 9110);

  Server_Transport_Layer transport;
  transport.slot_occupied[0] = true;
  transport.addresses[0] = wire.receiver_address;

  begin_paced_transfer(transport, 0, make_payload(40 * MAX_PAYLOAD_SIZE_IN_BYTES),
                       static_cast<uint8>(Message_Type::S2C_MapData));

  while (!transport.outbound_transfers[0].awaiting_receipt)
    service_paced_transfers(transport, wire.sender, 8);
  wire.drain();

  // The receiver got everything except fragments 3 and 31.
  transfer_receipt_t receipt;
  receipt.message_id = transport.outbound_transfers[0].message_id;
  receipt.fragment_count = 40;
  receipt.received_bits.assign(receipt_bitmap_size_in_bytes(40), 0);
  for (uint16 index = 0; index < 40; ++index)
    if (index != 3 && index != 31)
      receipt_mark_fragment(receipt, index);

  apply_transfer_receipt(transport, 0, receipt);
  assert(!transport.outbound_transfers[0].awaiting_receipt &&
         "a receipt is what releases the next pass");
  assert(transport.outbound_transfers[0].in_progress());

  service_paced_transfers(transport, wire.sender, 8);
  const std::vector<uint16> repaired = wire.drain();

  assert(repaired.size() == 2 && "only the two gaps go out again");
  assert((repaired[0] == 3 && repaired[1] == 31) ||
         (repaired[0] == 31 && repaired[1] == 3));

  // And a receipt covering everything ends the transfer, with no separate done
  // flag to keep in step.
  for (uint16 index = 0; index < 40; ++index)
    receipt_mark_fragment(receipt, index);
  apply_transfer_receipt(transport, 0, receipt);
  assert(!transport.outbound_transfers[0].in_progress());

  service_paced_transfers(transport, wire.sender, 8);
  assert(wire.drain().empty());
  assert(transport.outbound_transfers[0].fragments.empty() &&
         "a completed transfer frees itself");

  printf("  a receipt naming two gaps cost two fragments to repair\n");
}

// A receipt comes off the wire, so a truncated or self-contradictory one must be
// refused rather than used to size a bitmap by guesswork.
static void test_receipt_round_trip_and_refusal()
{
  transfer_receipt_t original;
  original.message_id = 42;
  original.fragment_count = 20;
  original.received_bits.assign(receipt_bitmap_size_in_bytes(20), 0);
  receipt_mark_fragment(original, 0);
  receipt_mark_fragment(original, 7);
  receipt_mark_fragment(original, 8);
  receipt_mark_fragment(original, 19);

  const std::vector<uint8> wire_bytes = serialize_transfer_receipt(original);
  assert(wire_bytes.size() == 3 + 3 && "20 fragments is three bitmap bytes");

  transfer_receipt_t decoded;
  assert(try_deserialize_transfer_receipt(wire_bytes, decoded));
  assert(decoded.message_id == 42);
  assert(decoded.fragment_count == 20);
  for (uint16 index = 0; index < 20; ++index)
    assert(receipt_holds_fragment(decoded, index) ==
           receipt_holds_fragment(original, index));

  std::vector<uint8> truncated = wire_bytes;
  truncated.pop_back();
  assert(!try_deserialize_transfer_receipt(truncated, decoded) &&
         "a bitmap shorter than the count it declares must be refused");

  std::vector<uint8> empty_count = wire_bytes;
  empty_count[1] = 0;
  empty_count[2] = 0;
  assert(!try_deserialize_transfer_receipt(empty_count, decoded) &&
         "a receipt about zero fragments names no transfer");

  printf("  receipt round trip, and both malformed shapes refused\n");
}

// A receipt for a message this slot is not sending is the ROUTINE case, not an
// error: the receiver reports every bucket it has trouble with, and a snapshot
// that lost a fragment is one of those.
static void test_a_receipt_for_another_message_is_ignored()
{
  Server_Transport_Layer transport;
  transport.slot_occupied[0] = true;

  begin_paced_transfer(transport, 0, make_payload(4 * MAX_PAYLOAD_SIZE_IN_BYTES),
                       static_cast<uint8>(Message_Type::S2C_MapData));
  transport.outbound_transfers[0].awaiting_receipt = true;

  transfer_receipt_t stranger;
  stranger.message_id =
      static_cast<uint8>(transport.outbound_transfers[0].message_id + 1);
  stranger.fragment_count = 2;
  stranger.received_bits.assign(receipt_bitmap_size_in_bytes(2), 0);
  receipt_mark_fragment(stranger, 0);

  apply_transfer_receipt(transport, 0, stranger);

  assert(transport.outbound_transfers[0].awaiting_receipt &&
         "an unrelated receipt must not release our pass");
  for (bool confirmed : transport.outbound_transfers[0].confirmed)
    assert(!confirmed && "nor confirm any of our fragments");

  printf("  a receipt naming another message was ignored\n");
}

// A re-request replaces the in-flight transfer instead of queueing behind it:
// the client re-requests precisely because it stopped reassembling the old one.
static void test_rerequest_replaces_transfer()
{
  Server_Transport_Layer transport;
  transport.slot_occupied[0] = true;

  begin_paced_transfer(transport, 0, make_payload(40 * MAX_PAYLOAD_SIZE_IN_BYTES),
                       static_cast<uint8>(Message_Type::S2C_MapData));
  transport.outbound_transfers[0].cursor = 12;
  transport.outbound_transfers[0].confirmed[0] = true;
  transport.outbound_transfers[0].awaiting_receipt = true;

  begin_paced_transfer(transport, 0, make_payload(3 * MAX_PAYLOAD_SIZE_IN_BYTES),
                       static_cast<uint8>(Message_Type::S2C_MapData));

  assert(transport.outbound_transfers[0].fragments.size() == 3 &&
         "the new payload must replace the old one, not append to it");
  assert(transport.outbound_transfers[0].confirmed.size() == 3 &&
         "the confirmed set must be resized with it");
  assert(transport.outbound_transfers[0].cursor == 0 &&
         "the cursor must restart, or the new transfer skips its first fragments");
  assert(!transport.outbound_transfers[0].awaiting_receipt &&
         "a fresh transfer has not completed a pass, so it is not waiting");
  for (bool confirmed : transport.outbound_transfers[0].confirmed)
    assert(!confirmed && "the previous transfer's confirmations mean nothing here");

  printf("  re-request replaced the in-flight transfer and reset its state\n");
}

// An incomplete bucket is dropped once it goes quiet, and -- the half that is
// easy to get wrong -- one still receiving fragments is NOT.
static void test_stale_buckets_expire()
{
  std::map<uint8, Partial_Message> partial_packets;

  const auto now = std::chrono::steady_clock::now();
  const auto beyond_timeout =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(partial_message_timeout_in_seconds + 1.0));

  // Bucket 7: two of four fragments, last seen well past the timeout.
  Partial_Message &stale = partial_packets[7];
  stale.fragments.resize(4);
  stale.fragments[0].header.fragment_count = 4;
  stale.fragments[0].header.payload_size = 16;
  stale.last_fragment_time = now - beyond_timeout;

  // Bucket 9: equally incomplete, but a fragment landed just now.
  Partial_Message &live = partial_packets[9];
  live.fragments.resize(4);
  live.fragments[0].header.fragment_count = 4;
  live.fragments[0].header.payload_size = 16;
  live.last_fragment_time = now;

  expire_stale_partial_messages(partial_packets, "test");

  assert(!partial_packets.contains(7) && "a bucket gone quiet must be dropped");
  assert(partial_packets.contains(9) &&
         "a transfer still receiving fragments must never be expired out from "
         "under itself");

  printf("  stale bucket expired, active bucket survived\n");
}

// The failure the expiry exists to prevent: message_id is a uint8, so a bucket
// left open forever swallows the message that draws the same id 256 sends later.
static void test_expiry_frees_the_id_for_reuse()
{
  std::map<uint8, Partial_Message> partial_packets;

  Partial_Message &abandoned = partial_packets[42];
  abandoned.fragments.resize(9); // eight of these never arrived
  abandoned.fragments[0].header.fragment_count = 9;
  abandoned.fragments[0].header.payload_size = 32;
  abandoned.last_fragment_time =
      std::chrono::steady_clock::now() -
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(partial_message_timeout_in_seconds * 2));

  expire_stale_partial_messages(partial_packets, "test");

  assert(partial_packets.empty() &&
         "id 42 must be free again, or the next message using it is eaten");

  printf("  abandoned bucket released its message_id\n");
}

// The drain cap's whole claim is that it sits ABOVE any backlog the kernel can
// hand us, so it never truncates a real drain and only ever stops a flood. That
// is checkable against the buffer size rather than a matter of taste, so check
// it -- a cap below the queue's capacity would silently strand datagrams every
// frame and look exactly like packet loss.
static void test_drain_cap_exceeds_queue_capacity()
{
  static_assert(client_receive_drain_cap_in_datagrams >
                    client_receive_buffer_size_in_bytes / MAX_PACKET_SIZE_IN_BYTES,
                "the client cap must exceed a full queue, or a legitimate "
                "backlog is truncated rather than drained");
  static_assert(server_receive_drain_cap_in_datagrams >
                    server_receive_buffer_size_in_bytes / MAX_PACKET_SIZE_IN_BYTES,
                "the server cap must exceed a full queue");

  // The server takes every peer's traffic on one socket, so its queue -- and
  // therefore its cap -- has to be the larger of the two.
  static_assert(server_receive_drain_cap_in_datagrams >
                    client_receive_drain_cap_in_datagrams,
                "the server drains an aggregate of all clients");

  printf("  drain caps: client %zu, server %zu datagrams "
         "(queues hold <= %zu / %zu)\n",
         client_receive_drain_cap_in_datagrams,
         server_receive_drain_cap_in_datagrams,
         client_receive_buffer_size_in_bytes / MAX_PACKET_SIZE_IN_BYTES,
         server_receive_buffer_size_in_bytes / MAX_PACKET_SIZE_IN_BYTES);
}

int main()
{
  printf("paced_transfer_test\n");
  test_drain_cap_exceeds_queue_capacity();
  test_transfer_is_paced();
  test_a_pass_waits_for_a_receipt();
  test_only_the_missing_fragments_are_resent();
  test_receipt_round_trip_and_refusal();
  test_a_receipt_for_another_message_is_ignored();
  test_rerequest_replaces_transfer();
  test_stale_buckets_expire();
  test_expiry_frees_the_id_for_reuse();
  printf("paced_transfer_test: all passed\n");
  return 0;
}
