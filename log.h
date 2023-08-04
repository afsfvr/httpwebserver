#ifndef LOG_H_
#define LOG_H_

#include <string>

#include "block_queue.h"

class Log{
    public:
        static Log* getInstance();
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

#define LOG_ERROR(format, ...) Log::getInstance()->write_log(1, __FILE__, __LINE__, format, ##__VA_ARGS__);Log::getInstance()->flush();
#define LOG_WARN(format, ...) Log::getInstance()->write_log(2, __FILE__, __LINE__, format, ##__VA_ARGS__);Log::getInstance()->flush();
#define LOG_INFO(format, ...) Log::getInstance()->write_log(3, __FILE__, __LINE__, format, ##__VA_ARGS__);
#define LOG_DEBUG(format, ...) Log::getInstance()->write_log(4, __FILE__, __LINE__, format, ##__VA_ARGS__);

#endif
