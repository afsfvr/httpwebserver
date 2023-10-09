#ifndef REDIS_POOL_H_
#define REDIS_POOL_H_

#include <pthread.h>

#include "redis.h"
class RedisPool;
class RedisConn {
public:
    RedisConn(RedisPool *pool, Redis *redis);
    RedisConn(RedisConn&&);
    RedisConn(const RedisConn&) = delete;
    RedisConn& operator=(const RedisConn&) = delete;
    ~RedisConn();
    operator bool() const;
    Redis* operator->() const ;
    Redis& operator*() const;
private:
    RedisPool *m_pool;
    Redis *m_redis;
};

class RedisPool {
public:
    RedisPool(int minIdle, int maxIdle, int maxCount, const char *url, const int port, const char *username=nullptr, const char *password=nullptr);
    ~RedisPool();
    int getIdleCount();
    RedisConn get();
    void put(Redis *redis);
private:
    void adjustPool();
    int m_min_idle;
    int m_max_idle;
    int m_max_count;
    int m_idle_count;
    int m_use_count;
    char *m_url;
    const int m_port;
    char *m_username;
    char *m_password;
    Redis **m_redis;
    bool *m_idle;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif
