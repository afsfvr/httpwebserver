#ifndef CONFIG_H_
#define CONFIG_H_

#ifndef CASE_INSENSITIVE_COMPARE_STRUCT
#define CASE_INSENSITIVE_COMPARE_STRUCT
struct case_insensitive_compare {
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](const char c1, const char c2){return std::tolower(c1) < std::tolower(c2);});
    }
};
#endif // CASE_INSENSITIVE_COMPARE_STRUCT

class Config {
public:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    static Config* getInstance();
    void parse(int argc, char *argv[]);
    int getPort() const { return port_; }
    int getThreadNum() const { return thread_num_; }
    const std::string& getWorkDirectory() const { return work_dir_; }
    bool isDaemon() const { return daemon_; }
    int getLogLevel() const { return log_level_; }
    const std::string& getLogFile() const { return log_file_; }
    const std::string& getWebappsPath() const { return webapps_path_; }
    const std::string& getRootUrl() const { return root_url_; }
#ifdef USE_REDIS
    const std::string& getRedisIp() const { return redis_ip_; }
    int getRedisPort() const { return redis_port_; }
    const std::string& getRedisName() const { return redis_name_; }
    const std::string& getRedisPasswd() const { return redis_passwd_; }
    int getRedisMinIdle() const { return redis_min_idle_; }
    int getRedisMaxIdle() const { return redis_max_idle_; }
    int getRedisMaxCount() const { return redis_max_count_; }
#endif // USE_REDIS
    const std::map<std::string, std::string, case_insensitive_compare>& getType() const { return type_; }
    bool allowIpv4() const { return ipv4_; }
    bool allowIpv6() const { return ipv6_; }
#ifdef HTTPS
    const std::string& getCertPath() const { return cert_; }
    const std::string& getKeyPath() const { return key_; }
#endif // HTTPS
private:
    Config();
    int port_{ 8888 };
    int thread_num_{ 8 };
    std::string work_dir_;
    bool daemon_{ false };
#ifndef NO_LOG
    int log_level_{ 2 };
    std::string log_file_;
#endif // NO_LOG
    std::string webapps_path_;
    std::string root_url_;
#ifdef USE_REDIS
    std::string redis_ip_{ "127.0.0.1" };
    int redis_port_{ 6379 };
    std::string redis_name_;
    std::string redis_passwd_;
    int redis_min_idle_{ 1 };
    int redis_max_idle_{ 4 };
    int redis_max_count_{ 8 };
#endif // USE_REDIS
    std::map<std::string, std::string, case_insensitive_compare> type_;
    bool ipv4_{ false };
    bool ipv6_{ false };
#ifdef HTTPS
    std::string cert_;
    std::string key_;
#endif // HTTPS
};

#endif // CONFIG_H_
