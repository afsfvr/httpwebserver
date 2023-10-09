#include <cstring>

#include "redis.h"

Redis::Redis(const char *ip, const int port, const char *username, const char *password) {
    m_context = redisConnect(ip, port);
    if (m_context == nullptr) throw std::string("无法分配redis上下文");
    if (m_context->err != 0) {
        std::string s(m_context->errstr);
        redisFree(m_context);
        throw s;
    }
    if (password != nullptr) {
        redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "AUTH %s %s", username, password));
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
            redisFree(m_context);
            if (reply == nullptr) {
                throw std::string("连接错误或无法获取 Redis 响应");
            } else {
                std::string s(reply->str);
                freeReplyObject(reply);
                throw s;
            }
        }
        freeReplyObject(reply);
    }
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "PING"));
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        redisFree(m_context);
        if (reply == nullptr) {
            throw std::string("连接错误或无法获取 Redis 响应");
        } else {
            std::string s(reply->str);
            freeReplyObject(reply);
            throw s;
        }
    }
    freeReplyObject(reply);
}

Redis::~Redis() {
    redisFree(m_context);
}

bool Redis::saveSession(uint64_t sessionId, int interval) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "HSETNX session:%llu sessionCreateTime %lu", sessionId, time(nullptr)));
    bool ret = false;
    if (reply != nullptr && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0) ret = true;
    if (ret && ! updateExpire(sessionId, interval)) {
        rmSession(sessionId);
        ret = false;
    }
    freeReplyObject(reply);
    return ret;
}

bool Redis::existsSession(uint64_t sessionId) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "TYPE session:%llu", sessionId));
    bool ret = false;
    if (reply != nullptr && (reply->type == REDIS_REPLY_STATUS || reply->type == REDIS_REPLY_STRING) && strcasecmp("hash", reply->str) == 0) ret = true;
    freeReplyObject(reply);
    return ret;
}

bool Redis::live() {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "PING"));
    bool ret = true;
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) ret = false;
    freeReplyObject(reply);
    return ret;
}

bool Redis::updateExpire(uint64_t sessionId, int interval) {
    std::string expireKey = std::string("expire:").append(std::to_string(sessionId));
    if (interval < 0) {
        std::string &&s = getData(expireKey.c_str());
        if (s.empty()) {
            interval = 600;
        } else {
            interval = std::stoi(s);
        }
    }
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "EXPIRE session:%llu %d", sessionId, interval));
    bool ret = false;
    if (reply != nullptr && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0) ret = true;
    freeReplyObject(reply);
    if (ret) setData(expireKey.c_str(), std::to_string(interval).c_str(), interval);
    return ret;
}

bool Redis::setData(const char *key, const char *value, int expire) {
    redisReply *reply;
    if (expire > 0) {
        reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "SET %s %s EX %d", key, value, expire));
    } else {
        reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "SET %s %s", key, value));
    }
    bool ret = true;
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) ret = false;
    freeReplyObject(reply);
    return ret;
}

bool Redis::addSessionData(uint64_t sessionId, const char *key, const char *value) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "HSETNX session:%llu %s %s", sessionId, key, value));
    bool ret = false;
    if (reply != nullptr && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0) ret = true;
    freeReplyObject(reply);
    return ret;
}

bool Redis::setSessionData(uint64_t sessionId, const char *key, const char *value) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "HSET session:%llu %s %s", sessionId, key, value));
    bool ret = true;
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) ret = false;
    freeReplyObject(reply);
    return ret;
}

std::string Redis::getData(const char *key) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "GET %s", key));
    std::string s;
    if (reply != nullptr && reply->type == REDIS_REPLY_STRING) s = reply->str;
    freeReplyObject(reply);
    return s;
}

std::string Redis::getSessionData(uint64_t sessionId, const char *key) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "HGET session:%llu %s", sessionId, key));
    std::string s;
    if (reply != nullptr && reply->type == REDIS_REPLY_STRING) s = reply->str;
    freeReplyObject(reply);
    return s;
}

std::map<std::string, std::string> Redis::getSessionData(uint64_t sessionId) {
    std::map<std::string, std::string> m;
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "HGETALL session:%llu", sessionId));
    if (reply->type == REDIS_REPLY_MAP || reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            m.emplace(reply->element[i]->str, reply->element[i + 1]->str);
        }
    }
    freeReplyObject(reply);
    return m;
}

bool Redis::rmSessionData(uint64_t sessionId, const char *key) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "HDEL session:%llu %s", sessionId, key));
    bool ret = false;
    if (reply != nullptr && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0) ret = true;
    freeReplyObject(reply);
    return ret;
}

bool Redis::rmSession(uint64_t sessionId) {
    redisReply *reply = reinterpret_cast<redisReply*>(redisCommand(m_context, "DEL session:%llu expire:%llu", sessionId, sessionId));
    bool ret = false;
    if (reply != nullptr && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0) ret = true;
    freeReplyObject(reply);
    return ret;
}
