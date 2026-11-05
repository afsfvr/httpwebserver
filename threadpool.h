#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <unistd.h>
#include <deque>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "config.h"

class Task {
public:
    virtual bool operator==(const Task *task) = 0;
    virtual void run() = 0;
    virtual ~Task() = default;
};

class ThreadPool {
public:
    ThreadPool();
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ~ThreadPool();
    bool cancelAndDeleteJob(Task *work);
    bool cancelJob(Task *work);
    bool addJob(Task *work);
private:
    struct ThreadData {
        std::thread thread;
        std::mutex mutex;
        Task* ptr;
        bool del;
    };
    void run(ThreadData *data);

    std::deque<Task *> m_jobs;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    int m_thread_num;
    ThreadData *m_threads;
    std::atomic_bool m_run;
};

#endif
