#include "threadpool.h"

ThreadPool::ThreadPool(): m_run(true) {
    m_thread_num = Config::getInstance()->getThreadNum();
    m_threads = new ThreadData[m_thread_num];
    std::lock_guard lock{ m_mutex };
    for (int i = 0; i < m_thread_num; i++) {
        m_threads[i].ptr = nullptr;
        m_threads[i].del = false;
        m_threads[i].thread = std::thread(&ThreadPool::run, this, &m_threads[i]);
        char name[32];
        snprintf(name, sizeof(name), "worker%d", i + 1);
        pthread_setname_np(m_threads[i].thread.native_handle(), name);
    }
}

ThreadPool::~ThreadPool() {
    m_run.store(false, std::memory_order::memory_order_release);
    m_cond.notify_all();
    usleep(1000 * 100);
    for (int i = 0; i < m_thread_num; i++) {
        pthread_cancel(m_threads[i].thread.native_handle());
    }
    for (int i = 0; i < m_thread_num; i++) {
        if (m_threads[i].thread.joinable()) {
            m_threads[i].thread.join();
        }
    }
    for (Task *task : m_jobs) {
        delete task;
    }
    m_jobs.clear();
    delete[] m_threads;
}

bool ThreadPool::cancelAndDeleteJob(Task *work) {
    if (work == nullptr) return false;
    std::lock_guard lock{ m_mutex };
    for (int i = 0; i < m_thread_num; ++i) {
        std::lock_guard lock{ m_threads[i].mutex };
        if (work == m_threads[i].ptr || *work == m_threads[i].ptr) {
            m_threads[i].del = true;
            return true;
        }
    }
    if (m_jobs.empty()) {
        return false;
    }
    for (auto iter = m_jobs.begin(); iter != m_jobs.end(); ++iter) {
        if (work == *iter || *work == *iter) {
            Task *task = *iter;
            m_jobs.erase(iter);
            delete task;
            return true;
        }
    }
    return false;
}

bool ThreadPool::cancelJob(Task *work) {
    if (work == nullptr) return false;
    std::lock_guard lock{ m_mutex };
    for (auto iter = m_jobs.begin(); iter != m_jobs.end(); ++iter) {
        if (work == *iter || *work == *iter) {
            m_jobs.erase(iter);
            return true;
        }
    }
    return false;
}

bool ThreadPool::addJob(Task *work) {
    if (!m_run.load(std::memory_order::memory_order_acquire)) return false;
    std::lock_guard lock{ m_mutex };
    m_jobs.push_back(work);
    m_cond.notify_one();
    return true;
}

void ThreadPool::run(ThreadData *data) {
    if (data == nullptr) return;
    while (m_run.load(std::memory_order::memory_order_acquire)) {
        std::unique_lock lock{ m_mutex };
        m_cond.wait(lock, [this]() { return ! m_jobs.empty() || ! m_run.load(std::memory_order::memory_order_acquire); });
        if (!m_run.load(std::memory_order::memory_order_acquire)) {
            return;
        }
        Task *work = m_jobs.front();
        m_jobs.pop_front();
        {
            std::lock_guard l{ data->mutex };
            data->ptr = work;
        }
        lock.unlock();
        work->run();
        {
            std::lock_guard l{ data->mutex };
            data->ptr = nullptr;
            if (data->del) {
                delete work;
                data->del = false;
            }
        }
    }
}

