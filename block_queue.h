#ifndef BLOCK_QUEUE_H_
#define BLOCK_QUEUE_H_

#include <pthread.h>
#include <cstring>
#include <unistd.h>

template<class T>
class BlockQueue {
public:
    BlockQueue();
    BlockQueue(const BlockQueue&) = delete;
    BlockQueue& operator=(const BlockQueue&) = delete;
    ~BlockQueue();
    bool push(const T &data);
    T pop();
private:
    class Data {
    public:
        Data(const T &data): m_data(data) {
            next = nullptr;
        }
        ~Data() {
            if (next != nullptr) {
                delete next;
            }
        }
        T m_data;
        Data *next;
    };
    Data *head;
    Data *tail;
    pthread_mutex_t mut;
    pthread_cond_t cond;
};

template<typename T>
BlockQueue<T>::BlockQueue() {
    pthread_mutex_init(&mut, nullptr);
    pthread_cond_init(&cond, nullptr);
    head = nullptr;
    tail = nullptr;
}

template<typename T>
BlockQueue<T>::~BlockQueue() {
    pthread_mutex_destroy(&mut);
    pthread_cond_destroy(&cond);
    if (head != nullptr) {
        delete head;
    }
}

template<typename T>
bool BlockQueue<T>::push(const T &data) {
    Data *d = new Data(data);
    pthread_mutex_lock(&mut);
    if (head == nullptr) {
        head = d;
        tail = d;
        pthread_cond_signal(&cond);
    } else {
        tail->next = d;
        tail = d;
    }
    pthread_mutex_unlock(&mut);
    return true;
}

template<typename T>
T BlockQueue<T>::pop() {
    pthread_mutex_lock(&mut);
    while (this->head == nullptr) {
        pthread_cond_wait(&cond, &mut);
    }
    Data *d = head;
    head = head->next;
    pthread_mutex_unlock(&mut);
    d->next = nullptr;
    T data = d->m_data;
    delete d;
    return data;
}

#endif
