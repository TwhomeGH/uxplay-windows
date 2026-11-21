/* Run the real RTP receive loop and buffer against scripted socket events.
 * This isolates control-only delivery and timer expiry from network timing. */
#include "../libuxplay/lib/raop_buffer.c"

static int scripted_select(int count, fd_set *read_set, fd_set *write_set,
                           fd_set *errors, const struct timeval *timeout);
static int scripted_receive(SOCKET socket, char *data, int length, int flags,
                            struct sockaddr *address, int *address_length);
#define select scripted_select
#define recvfrom scripted_receive
#include "../libuxplay/lib/raop_rtp.c"
#undef select
#undef recvfrom

static raop_rtp_t *active;
static int event_index, ticks, delivered, timer_case;

aes_ctx_t *aes_cbc_init(const uint8_t *key, const uint8_t *iv, aes_direction_t direction) {
    (void)key; (void)iv; (void)direction;
    return (aes_ctx_t *)malloc(1);
}
void aes_cbc_destroy(aes_ctx_t *ctx) { free(ctx); }
void aes_cbc_reset(aes_ctx_t *ctx) { (void)ctx; }
void aes_cbc_decrypt(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int length) {
    (void)ctx;
    memcpy(out, in, length);
}
void logger_log(logger_t *logger, int level, const char *format, ...) {
    (void)logger; (void)level; (void)format;
}
int logger_get_level(logger_t *logger) { (void)logger; return 0; }
char *wsa_strerror(int error) { (void)error; return "test socket"; }
uint64_t raop_ntp_get_local_time(void) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}
uint64_t raop_ntp_convert_remote_time(raop_ntp_t *ntp, uint64_t remote) {
    (void)ntp;
    return remote;
}
uint64_t raop_ntp_get_video_arrival_offset(raop_ntp_t *ntp) { (void)ntp; return 0; }

static int scripted_select(int count, fd_set *reads, fd_set *writes,
                           fd_set *errors, const struct timeval *timeout) {
    (void)count; (void)writes; (void)errors; (void)timeout;
    FD_ZERO(reads);
    assert(++ticks < 500); /* A broken loop must fail instead of hanging. */
    if (event_index < 2) {
        FD_SET(active->dsock, reads);
        return 1;
    }
    if ((timer_case == 0 || timer_case == 2) && event_index == 2) {
        FD_SET(active->csock, reads);
        return 1;
    }
    sleepms(5);
    return 0;
}

static int scripted_receive(SOCKET socket, char *data, int length, int flags,
                            struct sockaddr *address, int *address_length) {
    (void)length; (void)flags;
    unsigned char packet[17] = {0};
    const int offset = socket == (SOCKET)active->csock ? 4 : 0;
    const unsigned sequence = event_index == 0 ? 100 : event_index == 1 ? 102 : 101;
    if (offset) {
        packet[0] = 0x80;
        packet[1] = 0xd6;
        if (timer_case == 2) {
            memcpy(data, packet, 8);
            event_index++;
            return 8;
        }
        if (address) {
            memset(address, 0, *address_length);
            address->sa_family = AF_INET;
        }
    }
    packet[offset] = 0x80;
    packet[offset + 1] = 0x60;
    packet[offset + 2] = sequence >> 8;
    packet[offset + 3] = sequence & 255;
    packet[offset + 12] = 0x8c;
    memcpy(data, packet, offset + 13);
    event_index++;
    return offset + 13;
}

static void receive_audio(void *context, raop_ntp_t *ntp, audio_decode_struct *audio) {
    (void)context; (void)ntp;
    const unsigned expected = delivered == 0 ? 100 : timer_case ? 102 : 100 + delivered;
    assert(audio->seqnum == expected);
    if (++delivered == (timer_case ? 2 : 3)) active->running = 0;
}

int main(void) {
    raop_rtp_t clock_test = {0};
    clock_test.initial_sync = true;
    clock_test.rtp_sync = 0xffffff00U;
    clock_test.client_ntp_sync = 1000000000ULL;
    clock_test.rtp_clock_rate = 1000000000.0 / 44100.0;
    assert(rtp_time_to_client_ntp(&clock_test, 224) == 1010884353ULL);
    for (timer_case = 0; timer_case < 3; timer_case++) {
        const unsigned char key[16] = {0};
        raop_rtp_t worker = {0};
        worker.buffer = raop_buffer_init(NULL, key, key);
        worker.running = 1;
        worker.initial_sync = true;
        worker.ct = 8;
        worker.control_rport = 1;
        worker.csock = 11;
        worker.dsock = 12;
        worker.flush = NO_FLUSH;
        worker.callbacks.audio_process = receive_audio;
        worker.client_ntp_sync = raop_ntp_get_local_time();
        worker.rtp_clock_rate = 1000000000.0 / 44100.0;
        MUTEX_CREATE(worker.run_mutex);
        active = &worker;
        event_index = ticks = delivered = 0;
        const uint64_t start = buffer_now_ns();
        raop_rtp_thread_udp(&worker);
        assert(delivered == (timer_case ? 2 : 3));
        if (timer_case) {
            assert(buffer_now_ns() - start >= RAOP_GAP_WAIT_NS);
            assert(g_audio_stats.recoveries == 1);
            assert(g_audio_stats.empty_resends == (timer_case == 2 ? 1 : 0));
        }
        raop_buffer_destroy(worker.buffer);
        MUTEX_DESTROY(worker.run_mutex);
        g_audio_stats = (raop_rtp_audio_stats_t){0};
    }
    puts("RTP delivery tests passed: control-only resend, timeout recovery, empty reply without Debug");
    return 0;
}
