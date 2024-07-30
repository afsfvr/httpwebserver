#if ! defined (REDIS_H_) && ! defined (NO_REDIS)
#define REDIS_H_

#include <map>
#include <string>
#include <hiredis/hiredis.h>

class Redis {
public:
    Redis(const char *ip, const int port, const char *username=nullptr, const char *password=nullptr);
    ~Redis();
    bool saveSession(uint64_t sessionId, int interval=600);
    bool existsSession(uint64_t sessionId);
    bool live();
    bool updateExpire(uint64_t sessionId, int interval=-1);
    bool setData(const char *key, const char *value, int expire=-1);
    bool addSessionData(uint64_t sessionId, const char *key, const char *value);
    bool setSessionData(uint64_t sessionId, const char *key, const char *value);
    std::string getData(const char *key);
    std::string getSessionData(uint64_t sessionId, const char *key);
    std::map<std::string, std::string> getSessionData(uint64_t sessionId);
    bool rmSessionData(uint64_t sessionId, const char *key);
    bool rmSession(uint64_t sessionId);
private:
    redisContext *m_context;
};

#endif
