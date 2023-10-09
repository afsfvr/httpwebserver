#include "session.h"
#include "redis_pool.h"

extern RedisPool *pool;

Session::Session(uint64_t sessionId):m_sessionId(sessionId) {}

uint64_t Session::getId() const {
    return m_sessionId;
}

bool Session::addAttribute(const std::string& key, const std::string& value) const {
    if (m_sessionId == 0) return false;
    RedisConn redis = pool->get();
    if (! redis) return false;
    return redis->addSessionData(m_sessionId, key.c_str(), value.c_str());
}

bool Session::setAttribute(const std::string& key, const std::string& value) const {
    if (m_sessionId == 0) return false;
    RedisConn redis = pool->get();
    if (! redis) return false;
    return redis->setSessionData(m_sessionId, key.c_str(), value.c_str());
}

int Session::getMaxInactiveInterval() const {
    if (m_sessionId == 0) return 0;
    RedisConn redis = pool->get();
    if (! redis) return 0;
    try {
        return std::stoi(redis->getData(std::string("expire:").append(std::to_string(m_sessionId)).c_str()));
    } catch (std::exception&) {
        return 0;
    }
}

bool Session::setMaxInactiveInterval(int interval) const {
    if (m_sessionId == 0) return false;
    if (interval < 0) return false;
    RedisConn redis = pool->get();
    if (! redis) return -1;
    return redis->updateExpire(m_sessionId, interval);
}

time_t Session::getCreateTime() const {
    if (m_sessionId == 0) return 0;
    RedisConn redis = pool->get();
    if (! redis) return -1;
    std::string &&time = redis->getSessionData(m_sessionId, "sessionCreateTime");
    try {
        return std::stoul(time);
    } catch (std::exception&) {
        return 0;
    }
}

std::string Session::getAttribute(const std::string& key) const {
    if (m_sessionId == 0) return {};
    RedisConn redis = pool->get();
    if (! redis) return {};
    return redis->getSessionData(m_sessionId, key.c_str());
}

std::map<std::string, std::string> Session::getAttributes() const {
    if (m_sessionId == 0) return {};
    RedisConn redis = pool->get();
    if (! redis) return {};
    return redis->getSessionData(m_sessionId);
}

bool Session::removeAttribute(const std::string &key) const {
    if (m_sessionId == 0) return false;
    RedisConn redis = pool->get();
    if (! redis) return false;
    return redis->rmSessionData(m_sessionId, key.c_str());
}

bool Session::invalidate() {
    if (m_sessionId == 0) return false;
    RedisConn redis = pool->get();
    if (! redis) return false;
    if (! redis->rmSession(m_sessionId) && redis->existsSession(m_sessionId)) return false;
    return redis->saveSession(m_sessionId);
}
