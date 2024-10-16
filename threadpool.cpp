#include "threadpool.h"

ThreadPool::ThreadPool(): m_head(nullptr), m_tail(nullptr), m_run(true) {
    pthread_mutex_init(&m_mutex, nullptr);
    pthread_cond_init(&m_cond, nullptr);
    m_num = Config::getInstance()->getThreadNum();
    m_pids = new pthread_t[m_num];
    for (int i = 0; i < m_num; i++) {
        pthread_create(m_pids + i, nullptr, run, this);
    }
}

ThreadPool::~ThreadPool() {
    m_run = false;
    pthread_cond_broadcast(&m_cond);
    usleep(1000 * 100);
    for (int i = 0; i < m_num; i++) {
        pthread_cancel(m_pids[i]);
    }
    for (int i = 0; i < m_num; i++) {
        pthread_join(m_pids[i], nullptr);
    }
    if (m_head != nullptr) delete m_head;
    delete[] m_pids;
}

bool ThreadPool::canceljob(Task *work) {
    if (work == nullptr || m_head == nullptr) {
        return false;
    }
    pthread_mutex_lock(&m_mutex);
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
            pthread_mutex_unlock(&m_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&m_mutex);
    return false;
}

bool ThreadPool::addjob(Task* work) {
    if (! m_run) return false;
    pthread_mutex_lock(&m_mutex);
    Data *data = new Data(work);
    if (m_head == nullptr) {
        m_head = m_tail = data;
        pthread_cond_signal(&m_cond);
    } else {
        m_tail->next = data;
        m_tail = data;
    }
    pthread_mutex_unlock(&m_mutex);
    return true;
}

void* ThreadPool::run(void *p) {
    ThreadPool *pool = static_cast<ThreadPool*>(p);
    while (pool->m_run) {
        pthread_mutex_lock(&pool->m_mutex);
        while (pool->m_head == nullptr && pool->m_run) {
            pthread_cond_wait(&pool->m_cond, &pool->m_mutex);
        }
        if (! pool->m_run) {
            pthread_mutex_unlock(&pool->m_mutex);
            return nullptr;
        }
        Data *data = pool->m_head;
        pool->m_head = pool->m_head->next;
        pthread_mutex_unlock(&pool->m_mutex);
        data->next = nullptr;
        data->m_data->run();
        delete data;
    }
    pthread_exit(nullptr);
}
