#ifdef USE_REDIS

#include "redis_pool.h"
#include "config.h"

RedisConn::RedisConn(RedisPool *pool, Redis *redis): m_pool(pool), m_redis(redis) {}

RedisConn::RedisConn(RedisConn &&conn) {
    this->m_pool = conn.m_pool;
    this->m_redis = conn.m_redis;
    conn.m_pool = nullptr;
    conn.m_redis = nullptr;
}

RedisConn::~RedisConn() {
    if (m_pool != nullptr && m_redis != nullptr) m_pool->put(m_redis);
}

RedisConn::operator bool() const {
    return m_redis != nullptr;
}

Redis *RedisConn::operator->() const {
    return m_redis;
}

Redis &RedisConn::operator*() const {
    return *m_redis;
}

RedisPool::RedisPool(): m_idle_count(0), m_use_count(0) {
    Config *config = Config::getInstance();
    m_min_idle = config->getRedisMinIdle();
    m_max_idle = config->getRedisMaxIdle();
    m_max_count = config->getRedisMaxCount();
    m_url = config->getRedisIp();
    m_port = config->getRedisPort();
    m_username = config->getRedisName();
    m_password = config->getRedisPasswd();

    m_redis = new Redis * [m_max_count];
    m_idle = new bool[m_max_count];
    for (int i = 0; i < m_max_count; ++i) {
        if (m_idle_count >= m_min_idle) {
            m_redis[i] = nullptr;
            m_idle[i] = false;
            continue;
        }
        try {
            m_redis[i] = new Redis(m_url, m_port, m_username, m_password);
            ++m_idle_count;
            ++m_use_count;
            m_idle[i] = true;
        } catch (const std::string &e) {
            m_redis[i] = nullptr;
            m_idle[i] = false;
            SPDLOG_WARN("redis[{}]创建失败: {}", i, e);
            if (i >= 10 && m_use_count == 0) break;
        }
    }
    if (m_idle_count == 0 && m_min_idle > m_idle_count) {
        SPDLOG_ERROR("redis全部创建失败");
    } else {
        SPDLOG_INFO("成功创建{}个redis连接", m_use_count);
    }
}

RedisPool::~RedisPool() {
    m_mutex.lock();
    m_idle_count = 0;
    m_use_count = 0;
    for (int i = 0; i < m_max_count; ++i) {
        delete m_redis[i];
        m_redis[i] = nullptr;
        m_idle[i] = false;
    }
    cv.notify_all();
    m_mutex.unlock();
    std::this_thread::yield();
    delete[] m_redis;
    delete[] m_idle;
    m_url = nullptr;
    m_username = nullptr;
    m_password = nullptr;
    m_redis = nullptr;
    m_idle = nullptr;
}

int RedisPool::getIdleCount() {
    return m_idle_count;
}

RedisConn RedisPool::get() {
    if (m_redis == nullptr) return { nullptr, nullptr };
    if (m_idle_count <= 0) adjustPool();
    if (m_use_count == 0) {
        SPDLOG_ERROR("没有可用的redis连接,redis全部连接失败");
        return { nullptr, nullptr };
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    cv.wait(lock, [&]() {return m_idle_count > 0 || m_use_count == 0;});
    Redis *redis = nullptr;
    for (int i = 0; i < m_max_count; ++i) {
        if (m_idle[i]) {
            if (!m_redis[i]->live()) {
                delete m_redis[i];
                try {
                    m_redis[i] = new Redis(m_url, m_port, m_username, m_password);
                } catch (const std::string &e) {
                    SPDLOG_WARN("redis连接失败:{}", e);
                    m_redis[i] = nullptr;
                    --m_use_count;
                }
            }
            redis = m_redis[i];
            m_idle[i] = false;
            --m_idle_count;
            if (redis != nullptr) break;
        }
    }
    return { this, redis };
}

void RedisPool::put(Redis *redis) {
    if (redis == nullptr || m_use_count == 0) return;
    m_mutex.lock();
    for (int i = 0; i < m_max_count; ++i) {
        if (redis == m_redis[i] && !m_idle[i]) {
            if (m_idle_count + 1 > m_max_count) {
                delete redis;
                m_redis[i] = nullptr;
                --m_use_count;
            } else {
                m_idle[i] = true;
                if (m_idle_count == 0) cv.notify_one();
                ++m_idle_count;
            }
            break;
        }
    }
    m_mutex.unlock();
}

void RedisPool::adjustPool() {
    if (m_redis == nullptr) return;
    Redis *redis = nullptr;
    m_mutex.lock();
    for (int i = 0; m_use_count < m_max_count && m_idle_count <= m_min_idle; ++i) {
        if (redis == nullptr) {
            m_mutex.unlock();
            try {
                redis = new Redis(m_url, m_port, m_username, m_password);
            } catch (const std::string &e) {
                redis = nullptr;
                SPDLOG_WARN("redis连接失败:{}", e);
                return;
            }
            m_mutex.lock();
        }
        if (m_redis[i] == nullptr) {
            m_redis[i] = redis;
            redis = nullptr;
            m_idle[i] = true;
            ++m_idle_count;
            ++m_use_count;
        }
    }
    m_mutex.unlock();
    if (redis != nullptr) {
        delete redis;
        redis = nullptr;
    }
}

#endif
