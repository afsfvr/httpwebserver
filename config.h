#ifndef CONFIG_H_
#define CONFIG_H_

#include <string>
#include <map>
#include <pthread.h>

#ifndef CASE_INSENSITIVE_COMPARE_STRUCT
#define CASE_INSENSITIVE_COMPARE_STRUCT
struct case_insensitive_compare {
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](const char c1, const char c2){return std::tolower(c1) < std::tolower(c2);});
    }
};
#endif

class Config {
public:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    static Config* getInstance();
    void parse(int argc, char *argv[]);
    int getPort() const;
    int getThreadNum() const;
    const std::string& getWorkDirectory() const;
    const std::string& getDaemon() const;
    int getLogLevel() const;
    bool isAsyncWriteLog() const;
    const std::string& getWebappsPath() const;
    const std::string& getRootUrl() const;
    const std::string& getRedisIp() const;
    int getRedisPort() const;
    const std::string& getRedisName() const;
    const std::string& getRedisPasswd() const;
    int getRedisMinIdle() const;
    int getRedisMaxIdle() const;
    int getRedisMaxCount() const;
    const std::map<std::string, std::string, case_insensitive_compare>& getType() const;
private:
    Config();
    int m_port;
    int m_thread_num;
    std::string m_work_dir;
    std::string m_daemon;
    int m_log_level;
    bool m_async_write_log;
    std::string m_webapps_path;
    std::string m_root_url;
    std::string m_redis_ip;
    int m_redis_port;
    std::string m_redis_name;
    std::string m_redis_passwd;
    int m_redis_min_idle;
    int m_redis_max_idle;
    int m_redis_max_count;
    std::map<std::string, std::string, case_insensitive_compare> m_type;
};

#endif
