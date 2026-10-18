# CTAPHID parser review

Scope: `main/ctaphid/` (parser = `ctaphid.c`, command backbone = `ctaphid_commands.c`,
transport integration = `main/usb/usb_setup.c` and `main/tiny_touch.c`).

Summary: the reassembly state machine in `parse_packet()` is correct. Offsets,
lengths, sequence numbering, endianness and buffer bounds all check out; there is
no memory-safety bug and no off-by-one. The real problems are in edge handling and
I/O around the state machine, plus one protocol gap. Items are ordered roughly by
severity.

---

## 1. Silent drops on a full `reportQueue` can stall a transaction

**Where:** `ctaphid.c:35` (timeout event), `usb_setup.c:73` (USB OUT callback),
`usb_setup.c:92` (mock RX task).

**What:** every producer does `xQueueSend(reportQueue, &report, 0)` and ignores the
return value. With a full queue the event is dropped with no log.

**Why it matters:** if the dropped event is the `CTAP_EV_TIMEOUT`, the parser stays
in `ASSEMBLING` with a one-shot timer that has already fired and is now inactive —
nothing re-arms it. A subsequent `INIT` on any *other* channel gets
`CTAPHID_ERR_CHANNEL_BUSY` and is rejected, so the device looks wedged until an
`INIT` arrives on the same channel (or broadcast handling happens to match). This
is a liveness hole, not just packet loss.

Queue depth is 32, so it needs a burst, but the failure is silent and hard to
diagnose.

**Fix options:** log on drop; make timeout recovery not depend on a single queued
event (e.g. re-check inactivity on every packet, or keep the deadline in ticks);
size the queue for the worst-case burst.

---

## 2. Responses can be silently lost; no backpressure

**Where:** `ctaphid.c:89-104` (`send_buffer`), callers at `ctaphid.c:68-71, 73-87`,
`ctaphid_commands.c:78`.

**What:** `send_buffer()` returns `false` when `tud_hid_ready()` is false. That
result is propagated out of `send_init()` / `send_error()` but **every caller
ignores it**. There is no retry and no wait for the previous transfer to complete.

**Why it matters:** two responses in quick succession (for example a `CHANNEL_BUSY`
reply followed by a timeout error) → the second is silently dropped. More
importantly, the moment handlers start sending real replies that exceed 57 bytes
(PING echo, CBOR responses) a single `tud_hid_report()` per packet with no
`tud_hid_report_complete_cb` coordination will not work — and there is no
`send_cont()` implementation at all.

**Fix options:** handle the failure (queue/retry), and add a continuation-send path
gated on `tud_hid_report_complete_cb` before implementing multi-packet responses.

---

## 3. Timer API return values ignored

**Where:** `ctaphid.c:119` (`xTimerStop`), `ctaphid.c:187,191,218,223`
(`xTimerStop` / `xTimerReset`).

**What:** `xTimerReset()` / `xTimerStop()` return `pdFAIL` when the timer command
queue is full (`CONFIG_FREERTOS_TIMER_QUEUE_LENGTH=10`). All call sites discard the
result.

**Why it matters:** a failed `xTimerReset()` means the deadline is not extended, so
a transaction aborted by "timeout" can actually have had every fragment in time. A
failed `xTimerStop()` leaves a stale event that the stale-check may or may not
catch (see item 4).

**Fix:** check the return value and fall back (e.g. force-stop / restart via
`xTimerChangePeriod`, or track last-activity ticks instead of relying on the timer
command queue).

---

## 4. Stale-timeout suppression is racy (subtle, low probability)

**Where:** `ctaphid.c:131-137` (timeout branch), `ctaphid.c:191,223` (`xTimerReset`).

**What:** the stale check `if (xTimerIsTimerActive(timeoutTimer)) return;` relies on
the timer daemon having applied an earlier `xTimerReset()`. But `xTimerReset()` only
*queues* a command; the `tmrSTATUS_IS_ACTIVE` bit is set when the timer daemon
processes it. The parser task runs at priority 5 (`tiny_touch.c:28`), the timer
daemon at priority 1 (`CONFIG_FREERTOS_TIMER_TASK_PRIORITY=1`).

**Why it matters:** a fragment that arrived inside the deadline but is processed
after the expiry event was generated can still observe the timer as inactive →
spurious `CTAPHID_ERR_MSG_TIMEOUT`. In the common case FIFO ordering makes the check
correct (a packet queued ahead of the timeout event re-arms the timer in time), which
is why this is narrow rather than constant.

**Fix:** make the timeout event carry a transaction generation/id and compare it, or
compare `xTaskGetTickCount()` against a stored last-activity tick — both remove the
dependency on the timer daemon's scheduling.

---

## 5. Protocol gap: no channel validation

**Where:** `ctaphid.c:139-142` (CID read), `ctaphid.c:196` (channel match),
`ctaphid_constants.h:30` (`CTAPHID_ERR_INVALID_CHANNEL`, unused).

**What:** any CID is accepted. There is no channel table and
`CTAPHID_ERR_INVALID_CHANNEL` is never returned. Also, the INIT-vs-CONT decision is
`packet_cmd & 0x80` (`ctaphid.c:145`), so a continuation packet whose sequence byte
has bit 7 set is misclassified as an INIT (the spec reserves that bit as 0).

**Why it matters:** the parser will process traffic on channels it never allocated,
and a malformed/malicious continuation can be reinterpreted as an INIT and reset the
transaction. Expected to be stubbed for now, but it is a lurking problem for
protocol correctness.

**Fix:** keep a set of allocated channels (populated by the `INIT` handler) and
reject unknown CIDs; treat INIT as `(byte & 0x80) != 0` only when the low bits form a
known command, and reject continuation bytes with bit 7 set.

---

## Minor / nits

- `ctaphid.c:33-35` — `ctaphid_timeout_cb` sends a `CTAP_REPORT_T` with an
  uninitialized `buf` (reads indeterminate memory; benign today because the timeout
  path returns before touching `buf`).
- `ctaphid.c:19` — `TAG` is non-`static` (every other translation unit uses
  `static const char *TAG`); a symbol-collision footgun.
- `usb_setup.c:84-90` — `mock_usb_rx_task` does not check `bytes_read == 64` and
  zero-pads short reads into a full 64-byte report (dormant on ESP32-S3; the USB
  callback path does check).
- `ctaphid.c:99-102` — the LINUX `send_buffer` treats a partial `write()` (bytes
  written < 64) as success.
- Dead definitions: `CTAPHID_CONT_PACKET` (`ctaphid.h:21`), `CTAPHID_TYPE_CONT`,
  `CTAPHID_BROADCAST_CID` (`ctaphid_constants.h`).
- `tiny_touch.c:14` — `memset(&report, 0, ...)` after `parse_packet` is dead; the
  struct is overwritten by the next `xQueueReceive`.

---

## Investigated and confirmed *not* bugs

These look suspicious but are correct — listed so they are not re-flagged:

- **BCNT byte order.** `send_init` (`ctaphid.c:81`) writes big-endian and the parser
  (`ctaphid.c:160-161`) reads then byte-swaps identically. Round-trips correctly.
- **No buffer overflow.** The invariant `current_offset + remaining == BCNT` holds on
  every path, `BCNT` is capped at `CTAPHID_MAX_MSG_SIZE` (= 7609 = 57 + 128*59 =
  `sizeof(message)`), and each copy is `MIN(payload, remaining)`. The two
  `nr_bytes_to_copy > CTAPHID_MAX_MSG_SIZE - current_offset` guards can never fire but
  are harmless.
- **Sequence numbering / sizes.** `expected_seq` 0…127 (uint8 can't wrap within the
  128-fragment maximum); INIT≤57 vs CONT=59 offsets are correct (payload at
  `buf+7` and `buf+5` respectively).
- **`reset_parser_state()` does not stop the timer.** Every path compensates: a
  single-packet INIT calls `xTimerStop` (`:187`), a multi-packet INIT calls
  `xTimerReset` (`:191`), and `abort_transaction` calls `xTimerStop` (`:119`). No
  leak.
- **FIFO ordering of the stale check** in the normal case (see item 4 for the narrow
  race).
- **`nullptr` in `.c` files.** Fine — GCC 15 defaults to gnu23 where `nullptr` is
  defined.
- **Cross-task `tud_hid_report()`** from the parser task is safe: `tud_hid_n_report`
  claims the IN endpoint under the USB device mutex (`usbd_edpt_claim`).
