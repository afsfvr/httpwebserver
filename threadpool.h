#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <pthread.h>
#include <unistd.h>

#include "config.h"

class Task {
public:
    virtual bool operator==(const Task *task)=0;
    virtual void run() = 0;
    virtual ~Task() = default;
};

class ThreadPool {
public:
    ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ~ThreadPool();
    bool canceljob(Task *work);
    bool addjob(Task* work);
    static void* run(void *p);
private:
    struct Data {
    public:
        Data(Task *data): m_data(data) {
            next = nullptr;
        }
        ~Data() {
            if (next != nullptr) {
                delete next;
                next = nullptr;
            }
        }
        Task *m_data;
        Data *next;
    };
    Data *m_head;
    Data *m_tail;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
    int m_num;
    pthread_t *m_pids;
    bool m_run;
};

#endif
