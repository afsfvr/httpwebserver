#ifndef LOG_H_
#define LOG_H_

#ifdef NO_LOG
#define LOG_ERROR(format, ...)
#define LOG_WARN(format, ...)
#define LOG_INFO(format, ...)
#define LOG_DEBUG(format, ...)
#else

#include <string>

#include "block_queue.h"

class Log{
    public:
        static Log* getInstance();
        Log(const Log&) = delete;
        Log& operator=(const Log&) = delete;
        void write_log(int level, const char *filename, int line, const char *format, ...);
        void flush();
        ~Log();
        static void* run(void *p);
    private:
        Log();
        void async_write_log();
        bool m_write_log;
        bool async;
        int def_level;
        pthread_t pid;
        BlockQueue<std::string> *queue;
        FILE *m_fp;
};

#define LOG_ERROR(format, ...) do { Log::getInstance()->write_log(1, __FILE__, __LINE__, format, ##__VA_ARGS__);Log::getInstance()->flush(); } while(0)
#define LOG_WARN(format, ...) do { Log::getInstance()->write_log(2, __FILE__, __LINE__, format, ##__VA_ARGS__);Log::getInstance()->flush(); } while(0)
#define LOG_INFO(format, ...) do { Log::getInstance()->write_log(3, __FILE__, __LINE__, format, ##__VA_ARGS__); } while(0)
#define LOG_DEBUG(format, ...) do { Log::getInstance()->write_log(4, __FILE__, __LINE__, format, ##__VA_ARGS__); } while(0)
#endif

#endif
