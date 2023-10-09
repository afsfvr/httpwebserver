
#include "log.h"
#include "redis_pool.h"

RedisConn::RedisConn(RedisPool *pool, Redis *redis): m_pool(pool), m_redis(redis) {}

RedisConn::RedisConn(RedisConn&& conn) {
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

Redis* RedisConn::operator->() const {
    return m_redis;
}

Redis& RedisConn::operator*() const {
    return *m_redis;
}

RedisPool::RedisPool(int minIdle, int maxIdle, int maxCount, const char *url, const int port, const char *username, const char *password): m_min_idle(minIdle), m_max_idle(maxIdle), m_max_count(maxCount), m_idle_count(0), m_use_count(0), m_port(port), m_username(nullptr), m_password(nullptr) {
    m_url = new char[strlen(url) + 1];
    strcpy(m_url, url);

    if (username != nullptr) {
        int start = 0, end = 0, i = 0;
        for (int i = 0; username[i] != '\0'; ++i) {
            if (isspace(username[i])) {
                if (start == i) {
                    ++start;
                } else if (! isspace(username[i - 1])) {
                    end = i;
                }
            }
        }
        if (end == 0) end = i;
        if (end - start + 1 > 1) {
            m_username = new char[end - start + 1];
            strncpy(m_username, username + start, end - start);
            m_username[end] = '\0';
        }
    }
    if (password != nullptr) {
        int start = 0, end = 0, i = 0;
        for (int i = 0; password[i] != '\0'; ++i) {
            if (isspace(password[i])) {
                if (start == i) {
                    ++start;
                } else if (! isspace(password[i - 1])) {
                    end = i;
                }
            }
        }
        if (end == 0) end = i;
        if (end - start + 1 > 1) {
            m_password = new char[end - start + 1];
            strncpy(m_password, password + start, end - start);
            m_password[end] = '\0';
        }
    }

    m_redis = new Redis*[maxCount];
    m_idle = new bool[maxCount];
    for (int i = 0; i < maxCount; ++i) {
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
        } catch (const std::string& e) {
            m_redis[i] = nullptr;
            m_idle[i] = false;
            LOG_WARN("redis[%d]创建失败:%s", i, e.c_str());
        }
    }
    if (m_idle_count == 0 && m_min_idle > m_idle_count) {
        LOG_ERROR("redis全部创建失败");
    } else {
        LOG_INFO("成功创建%d个redis连接", m_use_count);
    }
    pthread_mutex_init(&m_mutex, nullptr);
    pthread_cond_init(&m_cond, nullptr);
}

RedisPool::~RedisPool() {
    m_idle_count = m_max_count;
    m_use_count = 0;
    pthread_cond_broadcast(&m_cond);
    pthread_yield();
    if (m_username != nullptr) delete[] m_username;
    if (m_password != nullptr) delete[] m_password;
    for (int i = 0; i < m_max_count; ++i) {
        delete m_redis[i];
    }
    delete[] m_redis;
    delete[] m_idle;
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

int RedisPool::getIdleCount() {
    return m_idle_count;
}

RedisConn RedisPool::get() {
    if (m_idle_count <= 0) {
        adjustPool();
    }
    if (m_use_count == 0) {
        LOG_ERROR("没有可用的redis连接,redis全部连接失败");
        return {nullptr, nullptr};
    }
    pthread_mutex_lock(&m_mutex);
    while (m_idle_count <= 0) {
        pthread_cond_wait(&m_cond, &m_mutex);
    }
    Redis *redis = nullptr;
    for (int i = 0; i < m_max_count; ++i) {
        if (m_idle[i]) {
            if (! m_redis[i]->live()) {
                delete m_redis[i];
                try {
                    m_redis[i] = new Redis(m_url, m_port, m_username, m_password);
                } catch (const std::string &e) {
                    LOG_WARN("redis连接失败:%s",e.c_str());
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
    pthread_mutex_unlock(&m_mutex);
    return {this, redis};
}

void RedisPool::put(Redis *redis) {
    if (redis == nullptr) return;
    pthread_mutex_lock(&m_mutex);
    for (int i = 0; i < m_max_count; ++i) {
        if (redis == m_redis[i] && ! m_idle[i]) {
            if (m_idle_count + 1 > m_max_count) {
                delete redis;
                m_redis[i] = nullptr;
                --m_use_count;
            } else {
                m_idle[i] = true;
                ++m_idle_count;
                pthread_cond_signal(&m_cond);
            }
            break;
        }
    }
    pthread_mutex_unlock(&m_mutex);
}

void RedisPool::adjustPool() {
    if (m_use_count == m_max_count) return;
    Redis *redis = nullptr;
    pthread_mutex_lock(&m_mutex);
    for (int i = 0; m_use_count < m_max_count && m_idle_count <= m_min_idle; ++i) {
        if (redis == nullptr) {
            pthread_mutex_unlock(&m_mutex);
            try {
                redis = new Redis(m_url, m_port, m_username, m_password);
            } catch (const std::string &e) {
                redis = nullptr;
                LOG_WARN("redis连接失败:%s",e.c_str());
                return;
            }
            pthread_mutex_lock(&m_mutex);
        }
        if (m_redis[i] == nullptr) {
            m_redis[i] = redis;
            redis = nullptr;
            m_idle[i] = true;
            ++m_idle_count;
            ++m_use_count;
        }
    }
    pthread_mutex_unlock(&m_mutex);
    if (redis != nullptr) {
        delete redis;
        redis = nullptr;
    }
}
