#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <pthread.h>

#include "block_queue.h"
#include "config.h"

template<class T>
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();
    void addjob(T* work);
    static void* run(void *p);
private:
    BlockQueue<T*> queue;
    int threadNum;
    pthread_t *pids;
};

template<typename T>
ThreadPool<T>::ThreadPool() {
    threadNum = Config::getInstance()->getThreadNum();
    pids = new pthread_t[threadNum];
    for (int i = 0; i < threadNum; i++) {
        pthread_create(pids + i, nullptr, run, &queue);
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    queue.exit();
    for (int i = 0; i < threadNum; i++) {
        pthread_cancel(pids[i]);
    }
    for (int i = 0; i < threadNum; i++) {
        pthread_join(pids[i], nullptr);
    }
    delete[] pids;
}

template<typename T>
void ThreadPool<T>::addjob(T* work) {
    queue.push(work);
}

template<typename T>
void* ThreadPool<T>::run(void *p) {
    BlockQueue<T*> *queue = static_cast<BlockQueue<T*>*>(p);
    while (true) {
        T *work = queue->pop();
        work->run();
    }
    pthread_exit(nullptr);
}
#endif
