// The reliable byte stream — src/shared/network/reliable_stream.hpp.
//
// The whole mechanism is "exactly one block outstanding", and every property
// below is that constraint cashing out. What is worth guarding is not the
// arithmetic but the four things a naive implementation gets wrong:
//
//   duplicate suppression — the common case under loss is the sender resending
//                           a block whose ack was lost. Re-delivering it is a
//                           second kill-feed row, and exactly-once is the
//                           STREAM's job so the handlers can stop caring.
//   a straddling record   — a block boundary may fall in the middle of a
//                           record, which is what makes a record larger than a
//                           datagram representable at all.
//   the free rule         — equality with the block in flight, not >=, so a
//                           stale ack names a block already confirmed and frees
//                           nothing.
//   overflow              — a peer that stopped confirming while we kept
//                           queueing is REPORTED, never papered over by
//                           dropping the oldest records.
//
// The two sides are driven by hand here rather than over a socket: a block is a
// Span of bytes and a number, and a test that stood a UDP pair up would be
// testing the socket.
#include "../shared/network/reliable_stream.hpp"

#include <cstdio>
#include <string>
#include <vector>

using network::accept_reliable_block;
using network::confirm_reliable_block;
using network::cut_reliable_block;
using network::drain_reliable_records;
using network::next_reliable_block_number;
using network::queue_reliable_message;
using network::reliable_block_in_flight;
using network::reliable_outbound_has_overflowed;
using network::reliable_pending_bytes;
using network::Reliable_Stream;
using network::RELIABLE_BLOCK_BUDGET_IN_BYTES;
using network::RELIABLE_OUTBOUND_CAP_IN_BYTES;
using network::RELIABLE_RECORD_HEADER_SIZE_IN_BYTES;
using network::uint8;

static int failures = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failures;
}

static void check_equal(unsigned long long actual, unsigned long long expected,
                        const char* what)
{
  const bool ok = actual == expected;
  printf(ok ? "  ok   %s  (%llu)\n" : "  FAIL %s  (%llu, expected %llu)\n", what,
         actual, expected);
  if (!ok)
    ++failures;
}

// One record as the receiver hands it back.
struct delivered_record_t
{
  uint8 message_type = 0;
  std::string payload;
};

// Message types are opaque to the stream — it hands the byte back and the
// caller's table decides what it means. Named locally for the same reason
// subtick_test names its own button bits.
static constexpr uint8 TYPE_A = 7;
static constexpr uint8 TYPE_B = 11;

static std::vector<uint8> bytes_of(const std::string& text)
{
  return std::vector<uint8>(text.begin(), text.end());
}

static void queue_text(Reliable_Stream& stream, uint8 message_type,
                       const std::string& text)
{
  const std::vector<uint8> payload = bytes_of(text);
  queue_reliable_message(stream, message_type, payload);
}

// Moves whatever block is in flight from sender to receiver, and reports
// whether the receiver took it. Does NOT ack: the ack is a separate step so a
// test can lose one.
static bool deliver_block(const Reliable_Stream& sender, Reliable_Stream& receiver)
{
  return accept_reliable_block(receiver, sender.block_number,
                               reliable_block_in_flight(sender));
}

static std::vector<delivered_record_t> drain(Reliable_Stream& receiver)
{
  std::vector<delivered_record_t> out;
  drain_reliable_records(receiver, [&out](uint8 message_type,
                                          std::vector<uint8>&& payload) {
    out.push_back({message_type, std::string(payload.begin(), payload.end())});
  });
  return out;
}

// --- In-order delivery -------------------------------------------------------

static void test_records_arrive_in_order_exactly_once()
{
  printf("three records, no loss -> three records in order\n");

  Reliable_Stream sender;
  Reliable_Stream receiver;

  queue_text(sender, TYPE_A, "first");
  queue_text(sender, TYPE_B, "second");
  queue_text(sender, TYPE_A, "third");

  cut_reliable_block(sender);
  check_equal(sender.block_number, 1, "the first block is numbered 1");
  check(deliver_block(sender, receiver), "the receiver takes block 1");
  confirm_reliable_block(sender, receiver.received_through);

  const std::vector<delivered_record_t> records = drain(receiver);
  check_equal(records.size(), 3, "all three records came out of one block");
  if (records.size() == 3)
  {
    check(records[0].payload == "first" && records[0].message_type == TYPE_A,
          "record 0 is the first one queued");
    check(records[1].payload == "second" && records[1].message_type == TYPE_B,
          "record 1 keeps its own message type");
    check(records[2].payload == "third", "record 2 is last");
  }

  check_equal(drain(receiver).size(), 0, "a second drain hands back nothing");
}

// --- The lost ack ------------------------------------------------------------

static void test_a_lost_ack_resends_and_the_duplicate_is_discarded()
{
  printf("the ack is lost -> the block is resent and NOT re-delivered\n");

  Reliable_Stream sender;
  Reliable_Stream receiver;

  queue_text(sender, TYPE_A, "a death");
  cut_reliable_block(sender);
  check(deliver_block(sender, receiver), "the receiver takes block 1");

  check_equal(drain(receiver).size(), 1, "the record is delivered once");

  // The ack never reaches the sender, so its next tick sends the same block
  // again: the send path cannot tell a first transmission from a fortieth.
  const uint8 resent_number = sender.block_number;
  cut_reliable_block(sender);
  check_equal(sender.block_number, resent_number,
              "an unconfirmed block keeps its number across a resend");

  check(!deliver_block(sender, receiver),
        "the duplicate block is refused by the receiver");
  check_equal(drain(receiver).size(), 0,
              "no record is delivered a second time");

  // The receiver's report is unchanged, and it is what finally frees the block.
  confirm_reliable_block(sender, receiver.received_through);
  check_equal(sender.block_length, 0, "the ack frees the block");
  check_equal(reliable_pending_bytes(sender), 0, "nothing is left pending");
}

static void test_a_stale_ack_frees_nothing()
{
  printf("an ack naming an already-confirmed block frees nothing\n");

  Reliable_Stream sender;
  Reliable_Stream receiver;

  queue_text(sender, TYPE_A, "one");
  cut_reliable_block(sender);
  deliver_block(sender, receiver);
  confirm_reliable_block(sender, receiver.received_through);

  queue_text(sender, TYPE_B, "two");
  cut_reliable_block(sender);
  check_equal(sender.block_number, 2, "the second block is numbered 2");

  // A datagram that left before the peer saw block 2 still carries the old
  // report. Equality, not >=, is what makes it a no-op rather than a confirm of
  // bytes the peer never got.
  confirm_reliable_block(sender, 1);
  check_equal(sender.block_length, 5 + 3,
              "block 2 is still outstanding after the stale ack");
}

// --- A record across a block boundary ---------------------------------------

static void test_a_record_may_straddle_two_blocks()
{
  printf("a record larger than one block -> several blocks, one record\n");

  Reliable_Stream sender;
  Reliable_Stream receiver;

  // A byte over one block, so the record needs a second one and the second
  // block starts mid-payload.
  const std::string payload(RELIABLE_BLOCK_BUDGET_IN_BYTES + 1, 'x');
  queue_text(sender, TYPE_A, payload);

  cut_reliable_block(sender);
  check_equal(sender.block_length, RELIABLE_BLOCK_BUDGET_IN_BYTES,
              "the cut respects the datagram budget");
  check(deliver_block(sender, receiver), "the receiver takes block 1");
  confirm_reliable_block(sender, receiver.received_through);

  check_equal(drain(receiver).size(), 0,
              "an incomplete record is not delivered");

  cut_reliable_block(sender);
  check_equal(sender.block_length,
              RELIABLE_RECORD_HEADER_SIZE_IN_BYTES + payload.size() -
                  RELIABLE_BLOCK_BUDGET_IN_BYTES,
              "the second block carries the remainder");
  check(deliver_block(sender, receiver), "the receiver takes block 2");
  confirm_reliable_block(sender, receiver.received_through);

  const std::vector<delivered_record_t> records = drain(receiver);
  check_equal(records.size(), 1, "the straddling record is delivered once");
  if (records.size() == 1)
    check(records[0].payload == payload,
          "and its bytes survived the boundary intact");
}

// --- Self-batching -----------------------------------------------------------

static void test_records_queued_while_a_block_is_in_flight_share_the_next_one()
{
  printf("bytes queued mid-flight -> one block, not one per record\n");

  Reliable_Stream sender;
  Reliable_Stream receiver;

  queue_text(sender, TYPE_A, "first");
  cut_reliable_block(sender);

  // Two more arrive before the first block is confirmed. The cut is made by the
  // ACK clock, not the tick clock, so they wait and then travel together.
  queue_text(sender, TYPE_B, "second");
  queue_text(sender, TYPE_B, "third");
  cut_reliable_block(sender);
  check_equal(sender.block_number, 1,
              "no second block is cut while one is outstanding");

  deliver_block(sender, receiver);
  confirm_reliable_block(sender, receiver.received_through);
  check_equal(drain(receiver).size(), 1, "block 1 carried only the first record");

  cut_reliable_block(sender);
  check_equal(sender.block_number, 2, "the freed stream cuts block 2");
  deliver_block(sender, receiver);
  confirm_reliable_block(sender, receiver.received_through);
  check_equal(drain(receiver).size(), 2,
              "both records queued during the flight shared block 2");
}

// --- Reclaim -----------------------------------------------------------------

static void test_the_buffer_is_reclaimed_once_drained()
{
  printf("a fully confirmed stream releases its bytes and keeps its capacity\n");

  Reliable_Stream sender;
  Reliable_Stream receiver;

  queue_text(sender, TYPE_A, "something");
  const size_t capacity_after_queue = sender.outbound.capacity();

  cut_reliable_block(sender);
  deliver_block(sender, receiver);
  confirm_reliable_block(sender, receiver.received_through);

  check_equal(sender.outbound.size(), 0, "the outbound buffer is emptied");
  check_equal(sender.confirmed_bytes, 0, "and the offset is rebased to 0");
  check(sender.outbound.capacity() >= capacity_after_queue,
        "clear() kept the capacity, like event_stream_t::reset");

  // The next record starts from a clean stream and still numbers forward.
  queue_text(sender, TYPE_B, "next");
  cut_reliable_block(sender);
  check_equal(sender.block_number, 2,
              "block numbers do not restart when the buffer is reclaimed");
}

// --- Numbering ---------------------------------------------------------------

static void test_block_numbers_wrap_skipping_zero()
{
  printf("255 -> 1, because 0 means no block attached\n");

  check_equal(next_reliable_block_number(1), 2, "1 -> 2");
  check_equal(next_reliable_block_number(254), 255, "254 -> 255");
  check_equal(next_reliable_block_number(255), 1, "255 -> 1, never 0");

  // And the receiver's accept rule wraps with it, so the wrap is not a stall.
  Reliable_Stream sender;
  Reliable_Stream receiver;
  sender.block_number = 255;
  receiver.received_through = 255;

  queue_text(sender, TYPE_A, "across the wrap");
  cut_reliable_block(sender);
  check_equal(sender.block_number, 1, "the block after 255 is 1");
  check(deliver_block(sender, receiver), "the receiver takes it");
  check_equal(drain(receiver).size(), 1, "and the record comes out");
}

static void test_a_block_numbered_zero_is_never_accepted()
{
  printf("block 0 means no block attached, so it carries nothing\n");

  Reliable_Stream receiver;
  const std::vector<uint8> payload = bytes_of("junk");
  check(!accept_reliable_block(receiver, 0, payload),
        "a zero block number is refused");
  check_equal(receiver.inbound.size(), 0, "and nothing was appended");
}

// --- Overflow ----------------------------------------------------------------

static void test_overflow_is_reported_rather_than_dropped()
{
  printf("a peer that stops confirming -> reported, oldest records kept\n");

  Reliable_Stream sender;

  queue_text(sender, TYPE_A, "the oldest record");
  cut_reliable_block(sender);
  check(!reliable_outbound_has_overflowed(sender), "one record is not overflow");

  // The peer never acks, so nothing is ever freed and the queue keeps growing.
  const std::string filler(4096, 'y');
  while (!reliable_outbound_has_overflowed(sender))
    queue_text(sender, TYPE_B, filler);

  check(reliable_pending_bytes(sender) > RELIABLE_OUTBOUND_CAP_IN_BYTES,
        "the cap is reported once it is genuinely exceeded");

  // The point of the check being a QUERY: the stream did not silently make room
  // by discarding the front, so the caller's disconnect is the only outcome.
  check(sender.outbound[0] == TYPE_A,
        "the oldest record is still at the front, undropped");
  check_equal(sender.block_length, 5 + 17,
              "and the block in flight still names it");
}

int main()
{
  printf("--- reliable stream ---\n");

  test_records_arrive_in_order_exactly_once();
  test_a_lost_ack_resends_and_the_duplicate_is_discarded();
  test_a_stale_ack_frees_nothing();
  test_a_record_may_straddle_two_blocks();
  test_records_queued_while_a_block_is_in_flight_share_the_next_one();
  test_the_buffer_is_reclaimed_once_drained();
  test_block_numbers_wrap_skipping_zero();
  test_a_block_numbered_zero_is_never_accepted();
  test_overflow_is_reported_rather_than_dropped();

  printf(failures == 0 ? "\nreliable stream: all checks passed\n"
                       : "\nreliable stream: %d FAILED\n",
         failures);
  return failures == 0 ? 0 : 1;
}
