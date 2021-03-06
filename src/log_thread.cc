//
//  logging.cc
//

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <queue>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>

#include "io.h"
#include "log.h"
#include "queue_atomic.h"

#include "log_thread.h"

/* log_thread */

const bool log_thread::debug = false;
const int log_thread::flush_interval_msecs = 100;

log_thread::log_thread(int fd, size_t num_buffers) :
    file(fdopen(fd, "w")),
    num_buffers(num_buffers),
    log_buffers_free(num_buffers),
    log_buffers_inuse(num_buffers),
    last_time(0),
    running(true),
    writer_waiting(false),
    thread(&log_thread::mainloop, this)
{
    create_buffers();
}

log_thread::~log_thread()
{
    shutdown();
    write_logs();
    delete_buffers();
    fclose(file);
}

void log_thread::shutdown()
{
    bool val = true;
    if (running.compare_exchange_strong(val, false)) {
        log_cond.notify_one();
        thread.join();
    }
}

void log_thread::create_buffers()
{
    // create buffers
    for (size_t i = 0; i < num_buffers; i++) {
        char *buffer = new char[LOG_BUFFER_SIZE];
        if (!buffer || !log_buffers_free.push_back(buffer)) {
            log_error("%s: error creating log buffer", __func__);
        }
    }
    if (debug) {
        log_debug("log_thread created %d buffers, %d bytes per buffer", num_buffers, LOG_BUFFER_SIZE);
    }
}

void log_thread::delete_buffers()
{
    // delete buffers
    for (size_t i = 0; i < num_buffers; i++) {
        char *buffer = log_buffers_free.pop_front();
        if (buffer) {
            delete [] buffer;
        } else {
            log_error("%s: error deleting log buffer", __func__);
        }
    }
}

void log_thread::write_logs()
{
    char *buffer = nullptr;
    while((buffer = log_buffers_inuse.pop_front())) {
        io_result result = fwrite(buffer, 1, strlen(buffer), file);
        if (result.has_error()) {
            log_error("%s: error writing log entry", __func__);
        }
        if (!log_buffers_free.push_back(buffer)) {
            log_error("%s: error returning log buffer", __func__);
        }
    }
    fflush(file);
}

void log_thread::log(time_t current_time, const char* message)
{
retry:
    char *buffer = log_buffers_free.pop_front();
    if (!buffer) {
        // TODO add statistics on log_thread stalls
        if (debug) {
            log_debug("%s: no log buffers, waiting", __func__);
        }
        std::unique_lock<std::mutex> lock(log_mutex);
        writer_waiting.store(true);
        writer_cond.wait(lock);
        if (debug) {
            log_debug("%s: client thread woke up", __func__);
        }
        goto retry;
    }
    strncpy(buffer, message, LOG_BUFFER_SIZE - 1);
    buffer[LOG_BUFFER_SIZE - 1] = '\0';
    if (!log_buffers_inuse.push_back(buffer)) {
        log_error("%s: error pushing log buffer", __func__);
        return;
    }
    if (current_time != last_time) {
        last_time = current_time;
        log_cond.notify_one();
    }
}

void log_thread::mainloop()
{
    // mainloop
    while (running) {
        // wait on condition
        {
            std::unique_lock<std::mutex> lock(log_mutex);
            log_cond.wait_for(lock, std::chrono::milliseconds(flush_interval_msecs));
            write_logs();
        }
        bool val = true;
        if (writer_waiting.compare_exchange_strong(val, false)) {
            writer_cond.notify_all();
        }
    }
}
