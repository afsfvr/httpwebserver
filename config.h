#ifndef CONFIG_H_
#define CONFIG_H_

#include <string>
#include <map>
#include <pthread.h>

class Config {
public:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    static Config* getInstance();
    void parse(int argc, char *argv[]);
    int getPort() const;
    const std::string& getRootPath() const;
    const std::string& getMainFile() const;
    const std::string& getWebappsPath() const;
    const std::string& getWebappsSo() const;
    int getThreadNum() const;
    const char* getDaemon() const;
    const std::map<std::string, std::string>& getType() const;
    bool getAsyncWriteLog() const;
    int getLogLevel() const;
    const std::string& getRedisIp() const;
    int getRedisPort() const;
    const std::string* getRedisName() const;
    const std::string* getRedisPasswd() const;
    int getRedisMaxCount() const;
    int getRedisMinIdle() const;
    int getRedisMaxIdle() const;
private:
    Config();
    int port;
    std::string root_path;
    std::string main_file;
    std::string webapps_path;
    std::string webapps_so;
    bool async_write_log;
    int log_level;
    int threadNum;
    std::string daemon;
    std::map<std::string, std::string> type;
    std::string redis_ip;
    int redis_port;
    std::string redis_name;
    std::string redis_passwd;
    int redis_min_idle;
    int redis_max_idle;
    int redis_max_count;
};

#endif
