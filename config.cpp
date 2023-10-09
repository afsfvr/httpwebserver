#include <iostream>
#include <getopt.h>
#include <unistd.h>

#include "config.h"

Config::Config(): redis_ip("127.0.0.1"), redis_port(6379), redis_name(""), redis_passwd(""), redis_min_idle(1), redis_max_idle(4), redis_max_count(8) {
    port = 8888;
    char *buf = get_current_dir_name();
    root_path = buf;
    webapps_path = buf;
    free(buf);
    root_path.append("/root/");
    webapps_path.append("/webapps/");
    webapps_so = "/main.so";
    main_file = "/index.html";
    async_write_log = false;
    log_level = 3;
    threadNum = 8;
    type.emplace("css", "text/css");
    type.emplace("xml", "application/xml");
    type.emplace("png", "image/png");
    type.emplace("jpeg", "image/jpeg");
    type.emplace("txt", "text/plain");
    type.emplace("ico", "image/x-icon");
    type.emplace("avi", "video/x-msvideo");
    type.emplace("json", "application/json");
    type.emplace("htm", "text/html");
    type.emplace("mp4", "video/mp4");
    type.emplace("mp3", "audio/mpeg");
    type.emplace("pdf", "application/pdf");
    type.emplace("js", "application/javascript");
    type.emplace("html", "text/html");
    type.emplace("gif", "image/gif");
    type.emplace("jpg", "image/jpeg");
    type.emplace("other", "application/octet-stream");
}

Config* Config::getInstance() {
    static Config config;
    return &config;
}

void Config::parse(int argc, char *argv[]) {
    if (argc == 0 || argv == nullptr) {
        throw std::invalid_argument(std::string("参数非法:nullptr"));
    }
    struct option argarr[] = {{"port", 1, nullptr, 'p'}, {"rootpath", 1, nullptr, 'r'}, {"file", 1, nullptr, 'f'}, {"thread", 1 , nullptr, 't'}, {"level", 1, nullptr, 'l'}, {"async", 0, nullptr, 'a'}, {"so", 1, nullptr, 's'}, {"webapps", 1, nullptr, 'w'}, {"redisip", 1, nullptr, 'i'}, {"redisport", 1, nullptr, 'P'}, {"username", 1, nullptr, 'u'}, {"secret", 1, nullptr, 'S'}, {"minIdle", 1, nullptr, 'm'}, {"maxIdle", 1, nullptr, 'M'}, {"count", 1, nullptr, 'c'}, {"help", 0, nullptr, 'h'}, {nullptr, 0, nullptr, 0}};
    int index = 0, c = -1;
    while ((c = getopt_long(argc, argv, "p:r:f:t:l:as:w:i:P:u:S:m:M:c:h", argarr, &index)) >= 0) {
        switch(c) {
        case 'p':
            this->port = atoi(optarg);
            break;
        case 'r':
            this->root_path = optarg;
            break;
        case 'f':
            this->main_file = optarg;
            break;
        case 't':
            this->threadNum = atoi(optarg);
            break;
        case 'l':
            this->log_level = atoi(optarg);
            break;
        case 'a':
            this->async_write_log = true;
            break;
        case 's':
            this->webapps_so = optarg;
            if (webapps_so[0] != '/') webapps_so.insert(0, 1, '/');
            break;
        case 'w':
            this->webapps_path = optarg;
            if (webapps_path.back() != '/') webapps_path.push_back('/');
            break;
        case 'i':
            this->redis_ip = optarg;
            break;
        case 'P':
            this->redis_port = atoi(optarg);
            break;
        case 'u':
            this->redis_name = optarg;
            break;
        case 'S':
            this->redis_passwd = optarg;
            break;
        case 'm':
            this->redis_min_idle = atoi(optarg);
            break;
        case 'M':
            this->redis_max_idle = atoi(optarg);
            break;
        case 'c':
            this->redis_max_count = atoi(optarg);
            break;
        case 'h':
            std::cout << "-p\t--port\t\t监听端口，默认8888" << std::endl;
            std::cout << "-r\t--rootpath\t静态资源路径" << std::endl;
            std::cout << "-f\t--file\t\t主页，默认index.html" << std::endl;
            std::cout << "-t\t--thread\t线程数量，默认8" << std::endl;
            std::cout << "-l\t--level\t\t日志级别,1-error,2-warn,3-info,4-debug,其它关闭日志，默认3" << std::endl;
            std::cout << "-a\t--async\t\t开启异步日志，默认同步日志" << std::endl;
            std::cout << "-s\t--so\t\t动态库so文件的名称，默认main.so" << std::endl;
            std::cout << "-w\t--webapps\t动态库所在路径，默认为工作目录下的webapps目录" << std::endl;
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
            std::cout << "参数非法，输入-h或--help查看帮助" << std::endl;
            exit(1);
        }
    }
}

int Config::getPort() const {
    return port;
}

const std::string& Config::getRootPath() const {
    return root_path;
}

const std::map<std::string, std::string>& Config::getType() const {
    return type;
}

const std::string& Config::getMainFile() const {
    return main_file;
}

int Config::getThreadNum() const {
    return threadNum;
}

bool Config::getAsyncWriteLog() const {
    return async_write_log;
}

int Config::getLogLevel() const {
    return log_level;
}

const std::string& Config::getWebappsPath() const {
    return webapps_path;
}

const std::string& Config::getWebappsSo() const {
    return webapps_so;
}

const std::string& Config::getRedisIp() const {
    return redis_ip;
}

int Config::getRedisPort() const {
    return redis_port;
}

const std::string* Config::getRedisName() const {
    if (redis_name.empty()) return nullptr;
    return &redis_name;
}

const std::string* Config::getRedisPasswd() const {
    if (redis_passwd.empty()) return nullptr;
    return &redis_passwd;
}

int Config::getRedisMaxCount() const {
    return redis_max_count;
}

int Config::getRedisMinIdle() const {
    return redis_min_idle;
}

int Config::getRedisMaxIdle() const {
    return redis_max_idle;
}
