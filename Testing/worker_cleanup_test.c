/* Exercise real stop functions after a worker has already exited. Build twice,
 * with and without TEST_NTP; unrelated networking functions are link-discarded. */
#ifdef TEST_NTP
#include "../libuxplay/lib/raop_ntp.c"
#else
#include "../libuxplay/lib/raop_rtp.c"
void raop_buffer_flush(raop_buffer_t *buffer, int sequence) {
    (void)buffer; (void)sequence;
}
#endif

void logger_log(logger_t *logger, int level, const char *fmt, ...) {
    (void)logger; (void)level; (void)fmt;
}

static void *finished_worker(void *argument) {
    (void)argument;
    return NULL;
}

int main(void) {
#ifdef TEST_NTP
    raop_ntp_t worker = {0};
    worker.tsock = -1;
    MUTEX_CREATE(worker.wait_mutex);
    COND_CREATE(worker.wait_cond);
#else
    raop_rtp_t worker = {0};
    worker.csock = worker.dsock = -1;
#endif
    MUTEX_CREATE(worker.run_mutex);
    for (int i = 0; i < 100; ++i) {
        worker.running = 0;
        worker.joined = 0;
        assert(pthread_create(&worker.thread, NULL, finished_worker, NULL) == 0);
#ifdef TEST_NTP
        raop_ntp_stop(&worker);
        raop_ntp_stop(&worker);
#else
        raop_rtp_stop(&worker);
        raop_rtp_stop(&worker);
#endif
        assert(worker.joined == 1 && worker.stopping == 0);
    }
    MUTEX_DESTROY(worker.run_mutex);
#ifdef TEST_NTP
    COND_DESTROY(worker.wait_cond);
    MUTEX_DESTROY(worker.wait_mutex);
#endif
    puts("worker cleanup regression test passed (100 cycles)");
    return 0;
}
