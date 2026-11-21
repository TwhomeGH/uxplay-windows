/* Regression tests for the real RTP buffer implementation. Crypto is stubbed:
 * these tests exercise ordering, duplicate packets and recovery, not AES. */
#include <time.h>
static int test_clock_gettime(clockid_t clock, struct timespec *now);
#define clock_gettime test_clock_gettime
#include "../libuxplay/lib/raop_buffer.c"
#undef clock_gettime

static uint64_t fake_now_ns = 1000000000ULL;
static int test_clock_gettime(clockid_t clock, struct timespec *now) {
    assert(clock == CLOCK_MONOTONIC);
    now->tv_sec = fake_now_ns / 1000000000ULL;
    now->tv_nsec = fake_now_ns % 1000000000ULL;
    return 0;
}

static unsigned resend_count = 0;
static int count_resend(void *opaque, unsigned short sequence, unsigned short count) {
    (void)opaque; (void)sequence;
    assert(count > 0);
    resend_count++;
    return 0;
}

aes_ctx_t *aes_cbc_init(const uint8_t *key, const uint8_t *iv, aes_direction_t direction) {
    (void)key; (void)iv; (void)direction;
    return (aes_ctx_t *)malloc(1);
}
void aes_cbc_destroy(aes_ctx_t *ctx) { free(ctx); }
void aes_cbc_reset(aes_ctx_t *ctx) { (void)ctx; }
void aes_cbc_decrypt(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int len) {
    (void)ctx;
    memcpy(out, in, len);
}
void logger_log(logger_t *logger, int level, const char *fmt, ...) {
    (void)logger; (void)level; (void)fmt;
}

static void receive_packet(raop_buffer_t *buffer, unsigned short sequence) {
    unsigned char packet[13] = {0x80, 0x60};
    packet[2] = sequence >> 8;
    packet[3] = sequence & 255;
    packet[12] = 0x8c;
    assert(raop_buffer_enqueue(buffer, packet, sizeof(packet), 1) >= 0);
}

static int take_packet(raop_buffer_t *buffer, int no_resend) {
    unsigned length;
    uint32_t timestamp;
    unsigned short sequence;
    void *payload = raop_buffer_dequeue(buffer, &length, &timestamp, &sequence, no_resend);
    if (!payload) return -1;
    assert(length == 1 && ((unsigned char *)payload)[0] == 0x8c);
    free(payload);
    return sequence;
}

int main(void) {
    const unsigned char key[16] = {0};
    raop_buffer_t *buffer = raop_buffer_init(NULL, key, key);
    receive_packet(buffer, 100);
    assert(take_packet(buffer, 0) == 100);
    receive_packet(buffer, 103);
    for (int i = 0; i < 100; i++) assert(take_packet(buffer, 0) == -1);
    assert(buffer->first_seqnum == 101); /* Repeated calls cannot age a gap. */
    receive_packet(buffer, 101); /* Timely resend remains playable. */
    assert(take_packet(buffer, 0) == 101);
    receive_packet(buffer, 134);
    assert(take_packet(buffer, 0) == 103); /* Skip overdue hole without returning NULL. */
    receive_packet(buffer, 135);
    raop_buffer_recover(buffer);
    assert(take_packet(buffer, 0) == 134); /* Recovery preserves buffered audio. */
    assert(take_packet(buffer, 0) == 135);

    raop_buffer_flush(buffer, -1);
    receive_packet(buffer, 200);
    assert(take_packet(buffer, 0) == 200);
    receive_packet(buffer, 205);
    receive_packet(buffer, 236);
    assert(take_packet(buffer, 0) == 205); /* Consecutive overdue holes drain in one call. */

    raop_buffer_flush(buffer, -1);
    receive_packet(buffer, 65534);
    assert(take_packet(buffer, 0) == 65534);
    receive_packet(buffer, 0);
    receive_packet(buffer, 30);
    assert(take_packet(buffer, 0) == 0); /* Missing 65535 across wraparound. */
    assert(take_packet(buffer, 1) == 30); /* No-resend mode skips holes immediately. */
    assert(take_packet(buffer, 1) == -1);

    raop_buffer_flush(buffer, -1);
    receive_packet(buffer, 10);
    receive_packet(buffer, 10);
    assert(take_packet(buffer, 0) == 10);
    assert(take_packet(buffer, 0) == -1);
    receive_packet(buffer, 9); /* Late data must not move the head backwards. */
    assert(take_packet(buffer, 0) == -1);

    raop_buffer_flush(buffer, -1);
    receive_packet(buffer, 300);
    assert(take_packet(buffer, 0) == 300);
    receive_packet(buffer, 303);
    raop_buffer_handle_resends(buffer, count_resend, NULL);
    raop_buffer_handle_resends(buffer, count_resend, NULL);
    assert(resend_count == 1);
    fake_now_ns += 20000000ULL;
    raop_buffer_handle_resends(buffer, count_resend, NULL);
    assert(resend_count == 2);
    fake_now_ns += 329000000ULL;
    assert(take_packet(buffer, 0) == -1); /* 349 ms: still allow retransmission. */
    fake_now_ns += 1000000ULL;
    assert(take_packet(buffer, 0) == 303); /* 350 ms: no future packet needed. */
    assert(raop_buffer_last_queue_wait_ms(buffer) == 350);
    uint64_t wait_ms;
    assert(raop_buffer_take_gap_skips(buffer, &wait_ms) == 2 && wait_ms == 350);
    assert(raop_buffer_take_gap_skips(buffer, &wait_ms) == 0);

    raop_buffer_flush(buffer, -1);
    receive_packet(buffer, 400);
    assert(take_packet(buffer, 0) == 400);
    unsigned char marker[16] = {0x80, 0x60, 0x01, 0x91}; /* sequence 401 */
    marker[13] = 0x68;
    marker[14] = 0x34;
    assert(raop_buffer_enqueue(buffer, marker, sizeof(marker), 1) == 1);
    receive_packet(buffer, 402);
    assert(take_packet(buffer, 0) == 402); /* Explicit silence is not packet loss. */
    assert(raop_buffer_take_gap_skips(buffer, &wait_ms) == 0);
    raop_buffer_destroy(buffer);
    puts("audio buffer regression tests passed");
    return 0;
}
