#include "threadpool.h"

ThreadPool::ThreadPool(): m_head(nullptr), m_tail(nullptr), m_run(true) {
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
    if (m_head != nullptr) delete m_head;
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
    if (m_head == nullptr) {
        return false;
    }
    for (Data *data = m_head, *front = nullptr; data != nullptr; front = data, data = data->next) {
        if (work == data->m_data || *work == data->m_data) {
            if (data == m_head) {
                m_head = m_head->next;
            } else {
                front->next = data->next;
                if (data->next == nullptr) {
                    m_tail = front;
                }
            }
            data->next = nullptr;
            delete data->m_data;
            delete data;
            return true;
        }
    }
    return false;
}

bool ThreadPool::cancelJob(Task *work) {
    std::lock_guard lock{ m_mutex };
    if (work == nullptr || m_head == nullptr) {
        return false;
    }
    for (Data *data = m_head, *front = nullptr; data != nullptr; front = data, data = data->next) {
        if (work == data->m_data || *work == data->m_data) {
            if (data == m_head) {
                m_head = m_head->next;
            } else {
                front->next = data->next;
                if (data->next == nullptr) {
                    m_tail = front;
                }
            }
            data->next = nullptr;
            delete data;
            return true;
        }
    }
    return false;
}

bool ThreadPool::addJob(Task *work) {
    if (!m_run.load(std::memory_order::memory_order_acquire)) return false;
    std::lock_guard lock{ m_mutex };
    Data *data = new Data(work);
    if (m_head == nullptr) {
        m_head = m_tail = data;
    } else {
        m_tail->next = data;
        m_tail = data;
    }
    m_cond.notify_one();
    return true;
}

void ThreadPool::run(ThreadData *data) {
    if (data == nullptr) return;
    while (m_run.load(std::memory_order::memory_order_acquire)) {
        std::unique_lock lock{ m_mutex };
        m_cond.wait(lock, [this]() { return m_head != nullptr || ! m_run.load(std::memory_order::memory_order_acquire); });
        if (!m_run.load(std::memory_order::memory_order_acquire)) {
            return;
        }
        Data *d = m_head;
        if (m_head == m_tail) {
            m_head = m_tail = nullptr;
        } else {
            m_head = m_head->next;
        }
        {
            std::lock_guard l{ data->mutex };
            data->ptr = d->m_data;
        }
        lock.unlock();
        d->next = nullptr;
        d->m_data->run();
        {
            std::lock_guard l{ data->mutex };
            data->ptr = nullptr;
            if (data->del) {
                delete d->m_data;
                data->del = false;
            }
        }
        delete d;
    }
}
