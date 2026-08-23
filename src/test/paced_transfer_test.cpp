// The sender-side flow control and the reassembly bucket's lifetime.
//
// Both exist because UDP gives us neither. A bulk message handed to the socket
// in one loop overruns the receiver's kernel queue -- loss that happens before
// its first recvfrom, which no receive-side change can recover -- and a message
// that loses a fragment leaves a bucket that never completes and eats whoever
// reuses its wrapped message_id. Undo either and a map download stops
// converging, so both get a test.

#include "shared/network/packet.hpp"
#include "shared/network/server_transport_layer.hpp"

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

// A transfer is handed out at the requested rate and no faster, and every
// fragment goes out exactly once and in order. The whole point is the RATE, so
// "it all arrived eventually" is not what is being asserted here.
static void test_transfer_is_paced()
{
  Server_Transport_Layer transport;
  transport.slot_occupied[0] = true;

  const std::vector<uint8> payload = make_payload(40 * MAX_PAYLOAD_SIZE_IN_BYTES);
  begin_paced_transfer(transport, 0, payload,
                       static_cast<uint8>(Message_Type::S2C_MapData));

  const size_t fragment_count = transport.outbound_transfers[0].fragments.size();
  assert(fragment_count == 40 && "payload should fragment into 40 packets");

  // Every fragment of one message shares an id -- that is what lets the receiver
  // group them, and it is why one message never spans two buckets.
  const uint8 message_id = transport.outbound_transfers[0].fragments[0].header.message_id;
  for (const Packet &fragment : transport.outbound_transfers[0].fragments)
    assert(fragment.header.message_id == message_id);

  // Walk it by hand at 8 per tick: no socket, so this checks the cursor rather
  // than delivery. Five ticks is exactly 40 fragments.
  size_t ticks = 0;
  while (transport.outbound_transfers[0].in_progress())
  {
    const size_t before = transport.outbound_transfers[0].next_fragment_index;
    const size_t sent_through = std::min(before + 8, fragment_count);
    transport.outbound_transfers[0].next_fragment_index = sent_through;

    assert(sent_through - before <= 8 && "a tick must never exceed the rate");
    ++ticks;
    assert(ticks <= 5 && "40 fragments at 8/tick must finish in 5 ticks");
  }
  assert(ticks == 5);

  printf("  paced transfer: %zu fragments in %zu ticks at 8/tick\n",
         fragment_count, ticks);
}

// A re-request replaces the in-flight transfer instead of queueing behind it:
// the client re-requests precisely because it stopped reassembling the old one.
static void test_rerequest_replaces_transfer()
{
  Server_Transport_Layer transport;
  transport.slot_occupied[0] = true;

  begin_paced_transfer(transport, 0, make_payload(40 * MAX_PAYLOAD_SIZE_IN_BYTES),
                       static_cast<uint8>(Message_Type::S2C_MapData));
  transport.outbound_transfers[0].next_fragment_index = 12;

  begin_paced_transfer(transport, 0, make_payload(3 * MAX_PAYLOAD_SIZE_IN_BYTES),
                       static_cast<uint8>(Message_Type::S2C_MapData));

  assert(transport.outbound_transfers[0].fragments.size() == 3 &&
         "the new payload must replace the old one, not append to it");
  assert(transport.outbound_transfers[0].next_fragment_index == 0 &&
         "the cursor must restart, or the new transfer skips its first fragments");

  printf("  re-request replaced the in-flight transfer and reset the cursor\n");
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
  test_rerequest_replaces_transfer();
  test_stale_buckets_expire();
  test_expiry_frees_the_id_for_reuse();
  printf("paced_transfer_test: all passed\n");
  return 0;
}
