#include <time.h>
#include <cstdarg>

#include "log.h"
#include "config.h"

static const int BUFSIZE = 2048;

Log::Log() {
    Config *config = Config::getInstance();
    async = config->getAsyncWriteLog();
    def_level = config->getLogLevel();
    m_write_log = true;
    if (def_level < 1 || def_level > 4) m_write_log = false;
    m_fp = stdout;
    if (async && m_write_log) {
        queue = new BlockQueue<std::string>;
        pthread_create(&pid, nullptr, run, nullptr);
    }
}

Log::~Log() {
    if (async && m_write_log) {
        pthread_cancel(pid);
        pthread_join(pid, nullptr);
        delete queue;
    }
    this->flush();
}

Log* Log::getInstance() {
    static Log log;
    return &log;
}

void Log::write_log(int level, const char *filename, int line, const char *format, ...) {
    if (! m_write_log || level > def_level) return;
    time_t t = time(NULL);
    struct tm *my_tm = localtime(&t);
    char buf[BUFSIZE];
    int n;
    if (level == 4) {
        n = snprintf(buf, BUFSIZE - 2, "%4d-%02d-%02d %02d:%02d:%02d [%s] %s:%d --> ", my_tm->tm_year + 1900, my_tm->tm_mon +1, my_tm->tm_mday, my_tm->tm_hour, my_tm->tm_min, my_tm->tm_sec, "DEBUG", filename, line);
    } else if (level == 3) {
        n = snprintf(buf, BUFSIZE - 2, "%4d-%02d-%02d %02d:%02d:%02d [%s] %s:%d --> ", my_tm->tm_year + 1900, my_tm->tm_mon +1, my_tm->tm_mday, my_tm->tm_hour, my_tm->tm_min, my_tm->tm_sec, "INFO ", filename, line);
    } else if (level == 2) {
        n = snprintf(buf, BUFSIZE - 2, "%4d-%02d-%02d %02d:%02d:%02d [%s] %s:%d --> ", my_tm->tm_year + 1900, my_tm->tm_mon +1, my_tm->tm_mday, my_tm->tm_hour, my_tm->tm_min, my_tm->tm_sec, "WARN ", filename, line);
    } else if (level == 1) {
        n = snprintf(buf, BUFSIZE - 2, "%4d-%02d-%02d %02d:%02d:%02d [%s] %s:%d --> ", my_tm->tm_year + 1900, my_tm->tm_mon +1, my_tm->tm_mday, my_tm->tm_hour, my_tm->tm_min, my_tm->tm_sec, "ERROR", filename, line);
    } else {
        return;
    }
    va_list ap;
    va_start(ap, format);
    int m = vsnprintf(buf + n, BUFSIZE - 2 - n, format, ap) + n;
    va_end(ap);
    int count = 0;
    for (int i = n; i < m && (count + m < BUFSIZE - 2); ++i) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == '\t' || buf[i] == '\f' || buf[i] == '\v') {
            ++count;
        }
    }
    buf[m + count] = '\n';
    buf[m + count +1 ] = '\0';
    for (int i = m - 1; i >= n && count > 0; --i) {
        if (buf[i] == '\r') {
            buf [i + count--] = 'r';
            buf[i + count] = '\\';
        } else if (buf[i] == '\n') {
            buf [i + count--] = 'n';
            buf[i + count] = '\\';
        } else if (buf[i] == '\t') {
            buf [i + count--] = 't';
            buf[i + count] = '\\';
        } else if (buf[i] == '\f') {
            buf [i + count--] = 'f';
            buf[i + count] = '\\';
        } else if (buf[i] == '\v') {
            buf [i + count--] = 'v';
            buf[i + count] = '\\';
        } else {
            buf[i + count] = buf[i];
        }
    }
    if (async) {
        queue->push(buf);
    } else {
        fputs(buf, m_fp);
    }
}

void Log::flush() {
    fflush(m_fp);
}

void* Log::run(void *p) {
    Log::getInstance()->async_write_log();
    pthread_exit(nullptr);
}

void Log::async_write_log() {
    while (true) {
        fputs(queue->pop().c_str(), m_fp);
    }
}
