#include <iostream>
#include <getopt.h>
#include <unistd.h>

#include "config.h"

Config::Config(): m_port(8888), m_thread_num(8), m_log_level(3), m_async_write_log(false), m_redis_ip("127.0.0.1"), m_redis_port(6379), m_redis_min_idle(1), m_redis_max_idle(4), m_redis_max_count(8), m_ipv4{false}, m_ipv6{false} {
    m_type.emplace("css", "text/css");
    m_type.emplace("xml", "application/xml");
    m_type.emplace("png", "image/png");
    m_type.emplace("jpeg", "image/jpeg");
    m_type.emplace("txt", "text/plain");
    m_type.emplace("ico", "image/x-icon");
    m_type.emplace("avi", "video/x-msvideo");
    m_type.emplace("json", "application/json");
    m_type.emplace("htm", "text/html");
    m_type.emplace("mp4", "video/mp4");
    m_type.emplace("mp3", "audio/mpeg");
    m_type.emplace("pdf", "application/pdf");
    m_type.emplace("js", "application/javascript");
    m_type.emplace("html", "text/html");
    m_type.emplace("gif", "image/gif");
    m_type.emplace("jpg", "image/jpeg");
    m_type.emplace("other", "application/octet-stream");
}

Config* Config::getInstance() {
    static Config config;
    return &config;
}

void Config::parse(int argc, char *argv[]) {
    if (argc == 0 || argv == nullptr) {
        throw std::invalid_argument(std::string("参数非法:nullptr"));
    }
    struct option argarr[] = {
        {"ipv4", 0, nullptr, '4'},
        {"ipv6", 0, nullptr, '6'},
        {"port", 1, nullptr, 'p'},
        {"thread", 1, nullptr, 't'},
        {"work", 1, nullptr, 'W'},
        {"daemon", 1, nullptr, 'd'},
        {"level", 1, nullptr, 'l'},
        {"async", 0, nullptr, 'a'},
        {"webapps", 1, nullptr, 'w'},
        {"rooturl", 1, nullptr, 'r'},
        {"redisip", 1, nullptr, 'i'},
        {"redisport", 1, nullptr, 'P'},
        {"username", 1, nullptr, 'u'},
        {"secret", 1, nullptr, 'S'},
        {"minIdle", 1, nullptr, 'm'},
        {"maxIdle", 1, nullptr, 'M'},
        {"count", 1, nullptr, 'c'},
        {"help", 0, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}};
    int index = 0, c = -1;
    while ((c = getopt_long(argc, argv, "46p:t:W:d:l:aw:r:i:P:u:S:m:M:c:h", argarr, &index)) >= 0) {
        switch(c) {
        case '4':
            this->m_ipv4 = true;
            break;
        case '6':
            this->m_ipv6 = true;
            break;
        case 'p':
            this->m_port = atoi(optarg);
            break;
        case 't':
            this->m_thread_num = atoi(optarg);
            break;
        case 'W':
            this->m_work_dir = optarg;
            break;
        case 'd':
            this->m_daemon = optarg;
            break;
        case 'l':
            this->m_log_level = atoi(optarg);
            break;
        case 'a':
            this->m_async_write_log = true;
            break;
        case 'w':
            this->m_webapps_path = optarg;
            break;
        case 'r':
            this->m_root_url = optarg;
            break;
        case 'i':
            this->m_redis_ip = optarg;
            break;
        case 'P':
            this->m_redis_port = atoi(optarg);
            break;
        case 'u':
            this->m_redis_name = optarg;
            break;
        case 'S':
            this->m_redis_passwd = optarg;
            break;
        case 'm':
            this->m_redis_min_idle = atoi(optarg);
            break;
        case 'M':
            this->m_redis_max_idle = atoi(optarg);
            break;
        case 'c':
            this->m_redis_max_count = atoi(optarg);
            break;
        case 'h':
            std::cout << "-4\t--ipv4\t\t允许ipv4访问(默认同时允许ipv4和ipv6)" << std::endl;
            std::cout << "-6\t--ipv6\t\t允许ipv6访问(默认同时允许ipv4和ipv6)" << std::endl;
            std::cout << "-p\t--port\t\t监听端口，默认8888" << std::endl;
            std::cout << "-t\t--thread\t线程数量，默认8" << std::endl;
            std::cout << "-W\t--work\t工作路径，需为绝对路径" << std::endl;
            std::cout << "-d\t--daemon\t守护进程日志路径" << std::endl;
            std::cout << "-l\t--level\t\t日志级别,1-error,2-warn,3-info,4-debug,其它关闭日志，默认3" << std::endl;
            std::cout << "-a\t--async\t\t开启异步日志，默认同步日志" << std::endl;
            std::cout << "-w\t--webapps\t动态库所在路径，默认为工作目录下的webapps目录" << std::endl;
            std::cout << "-r\t--rooturl\t根路径，即访问ip:port的路径,默认为/root/" << std::endl;
            std::cout << "-i\t--redisip\tredis的ip地址，默认为127.0.0.1" << std::endl;
            std::cout << "-P\t--redisport\tredis的端口，默认为6379" << std::endl;
            std::cout << "-u\t--username\tredis的用户名，默认为空" << std::endl;
            std::cout << "-S\t--secret\tredis的密码，默认为空" << std::endl;
            std::cout << "-m\t--minIdle\tredis连接池的最小空闲数" << std::endl;
            std::cout << "-M\t--maxIdle\tredis连接池的最大空闲数" << std::endl;
            std::cout << "-c\t--count\t\tredis连接池的最大数量" << std::endl;
            std::cout << "-h\t--help\t\t查看帮助" << std::endl;
            exit(0);
        default:
            std::cerr << "参数非法，输入-h或--help查看帮助" << std::endl;
            exit(1);
        }
    }

    if (! m_ipv4 && ! m_ipv6) {
        m_ipv4 = m_ipv6 = true;
    }
    if (m_work_dir.length() == 0) {
        char *buf = get_current_dir_name();
        m_work_dir = buf;
        free(buf);
    } else if (m_work_dir.front() != '/') {
        std::cerr << "工作路径需为绝对路径" << std::endl;
        exit(1);
    }
    if (m_work_dir.back() != '/') {
        m_work_dir.push_back('/');
    }

    if (m_webapps_path.length() == 0) {
        m_webapps_path = m_work_dir + "webapps/";
    } else {
        if (m_webapps_path.front() != '/') {
            m_webapps_path = m_work_dir + m_webapps_path;
        }
        if (m_webapps_path.back() != '/') {
            m_webapps_path.push_back('/');
        }
    }

    if (m_root_url.length() == 0) {
        m_root_url = "/root/";
    } else {
        if (m_root_url.front() != '/') {
            m_root_url.insert(m_root_url.begin(), '/');
        }
        if (m_root_url.back() != '/') {
            m_root_url.push_back('/');
        }
    }
}

int Config::getPort() const {
    return m_port;
}

int Config::getThreadNum() const {
    return m_thread_num;
}

const std::string& Config::getWorkDirectory() const {
    return m_work_dir;
}

const std::string& Config::getDaemon() const {
    return m_daemon;
}

int Config::getLogLevel() const {
    return m_log_level;
}

bool Config::isAsyncWriteLog() const {
    return m_async_write_log;
}

const std::string& Config::getWebappsPath() const {
    return m_webapps_path;
}

const std::string& Config::getRootUrl() const {
    return m_root_url;
}

const std::string& Config::getRedisIp() const {
    return m_redis_ip;
}

int Config::getRedisPort() const {
    return m_redis_port;
}

const std::string& Config::getRedisName() const {
    return m_redis_name;
}

const std::string& Config::getRedisPasswd() const {
    return m_redis_passwd;
}

int Config::getRedisMinIdle() const {
    return m_redis_min_idle;
}

int Config::getRedisMaxIdle() const {
    return m_redis_max_idle;
}

int Config::getRedisMaxCount() const {
    return m_redis_max_count;
}

const std::map<std::string, std::string, case_insensitive_compare>& Config::getType() const {
    return m_type;
}

bool Config::allowIpv4() const {
    return m_ipv4;
}

bool Config::allowIpv6() const {
    return m_ipv6;
}
