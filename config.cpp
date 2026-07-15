#include "config.h"

Config::Config() {
    type_.emplace("css", "text/css");
    type_.emplace("xml", "application/xml");
    type_.emplace("png", "image/png");
    type_.emplace("jpeg", "image/jpeg");
    type_.emplace("txt", "text/plain");
    type_.emplace("ico", "image/x-icon");
    type_.emplace("avi", "video/x-msvideo");
    type_.emplace("json", "application/json");
    type_.emplace("htm", "text/html");
    type_.emplace("mp4", "video/mp4");
    type_.emplace("mp3", "audio/mpeg");
    type_.emplace("pdf", "application/pdf");
    type_.emplace("js", "application/javascript");
    type_.emplace("html", "text/html");
    type_.emplace("gif", "image/gif");
    type_.emplace("jpg", "image/jpeg");
    type_.emplace("other", "application/octet-stream");
}

Config *Config::getInstance() {
    static Config config;
    return &config;
}

void Config::parse(int argc, char *argv[]) {
    if (argc == 0 || argv == nullptr) {
        throw std::invalid_argument(std::string("参数非法:nullptr"));
    }
    struct option argarr[] = {
#ifdef HTTPS
        {"ssl-cert", 1, nullptr, 0},
        {"ssl-key", 1, nullptr, 0},
#endif // HTTPS
        {"ipv4", 0, nullptr, '4'},
        {"ipv6", 0, nullptr, '6'},
        {"port", 1, nullptr, 'p'},
        {"threads", 1, nullptr, 't'},
        {"workdir", 1, nullptr, 'W'},
        {"daemon", 0, nullptr, 'd'},
#ifndef NO_LOG
#define LOG_STR "l:f:"
        {"log-level", 1, nullptr, 'l'},
        {"log-file", 1, nullptr, 'f'},
#else
#define LOG_STR ""
#endif // NO_LOG
        {"webapps", 1, nullptr, 'w'},
        {"root-url", 1, nullptr, 'r'},
#ifdef REDIS
#define REDIS_STR "i:P:u:S:m:M:c:"
        {"redis-ip", 1, nullptr, 'i'},
        {"redis-port", 1, nullptr, 'P'},
        {"redis-username", 1, nullptr, 'u'},
        {"redis-secret", 1, nullptr, 'S'},
        {"redis-min-idle", 1, nullptr, 'm'},
        {"redis-max-idle", 1, nullptr, 'M'},
        {"redis-count", 1, nullptr, 'c'},
#else
#define REDIS_STR ""
#endif // REDIS
        {"help", 0, nullptr, 'h'},
        {nullptr, 0, nullptr, 0} };
    int index = 0, c = -1;
    while ((c = getopt_long(argc, argv, "46p:t:W:d" LOG_STR "w:r:" REDIS_STR "h", argarr, &index)) >= 0) {
        switch (c) {
#ifdef HTTPS
        case 0:
            if (strncmp(argarr[index].name, "ssl-cert", 8) == 0) {
                cert_ = optarg;
            } else if (strncmp(argarr[index].name, "ssl-key", 7) == 0) {
                key_ = optarg;
            }
            break;
#endif // HTTPS
        case '4':
            this->ipv4_ = true;
            break;
        case '6':
            this->ipv6_ = true;
            break;
        case 'p':
            this->port_ = atoi(optarg);
            break;
        case 't':
            this->thread_num_ = atoi(optarg);
            break;
        case 'W':
            this->work_dir_ = optarg;
            break;
        case 'd':
            this->daemon_ = true;
            break;
#ifndef NO_LOG
        case 'l':
            if (strcasecmp(optarg, "trace") == 0) {
                this->log_level_ = 0;
            } else if (strcasecmp(optarg, "debug") == 0) {
                this->log_level_ = 1;
            } else if (strcasecmp(optarg, "info") == 0) {
                this->log_level_ = 2;
            } else if (strcasecmp(optarg, "warn") == 0) {
                this->log_level_ = 3;
            } else if (strcasecmp(optarg, "error") == 0) {
                this->log_level_ = 4;
            } else if (strcasecmp(optarg, "critical") == 0) {
                this->log_level_ = 5;
            } else if (strcasecmp(optarg, "off") == 0) {
                this->log_level_ = 6;
            } else {
                this->log_level_ = atoi(optarg);
                if (this->log_level_ < 0) {
                    this->log_level_ = 0;
                } else if (this->log_level_ > 7) {
                    this->log_level_ = 7;
                }
            }
            break;
        case 'f':
            this->log_file_ = optarg;
            break;
#endif // NO_LOG
        case 'w':
            this->webapps_path_ = optarg;
            break;
        case 'r':
            this->root_url_ = optarg;
            break;
#ifdef REDIS
        case 'i':
            this->redis_ip_ = optarg;
            break;
        case 'P':
            this->redis_port_ = atoi(optarg);
            break;
        case 'u':
            this->redis_name_ = optarg;
            break;
        case 'S':
            this->redis_passwd_ = optarg;
            break;
        case 'm':
            this->redis_min_idle_ = atoi(optarg);
            break;
        case 'M':
            this->redis_max_idle_ = atoi(optarg);
            break;
        case 'c':
            this->redis_max_count_ = atoi(optarg);
            break;
#endif // REDIS
        case 'h':
#ifdef HTTPS
            std::cout << "\t--ssl-cert\tssl证书位置" << std::endl;
            std::cout << "\t--ssl-key\tssl私钥位置" << std::endl;
#endif // HTTPS
            std::cout << "-4\t--ipv4\t\t允许ipv4访问(默认同时允许ipv4和ipv6)" << std::endl;
            std::cout << "-6\t--ipv6\t\t允许ipv6访问(默认同时允许ipv4和ipv6)" << std::endl;
            std::cout << "-p\t--port\t\t监听端口，默认8888" << std::endl;
            std::cout << "-t\t--threads\t线程数量，默认8" << std::endl;
            std::cout << "-W\t--workdir\t工作路径，需为绝对路径" << std::endl;
            std::cout << "-d\t--daemon\t守护进程日志路径" << std::endl;
#ifndef NO_LOG
            std::cout << "-l\t--log-level\t日志级别,0-trace,1-debug,2-info,3-warn,4-error,5-critical,6-off默认2" << std::endl;
            std::cout << "-f\t--log-file\t日志文件路径" << std::endl;
#endif //NO_LOG
            std::cout << "-w\t--webapps\t动态库所在路径，默认为工作目录下的webapps目录" << std::endl;
            std::cout << "-r\t--root-url\t根路径，即访问ip:port的路径,默认为/root/" << std::endl;
#ifdef REDIS
            std::cout << "-i\t--redis-ip\tredis的ip地址，默认为127.0.0.1" << std::endl;
            std::cout << "-P\t--redis-port\tredis的端口，默认为6379" << std::endl;
            std::cout << "-u\t--redis-username\tredis的用户名，默认为空" << std::endl;
            std::cout << "-S\t--redis-secret\tredis的密码，默认为空" << std::endl;
            std::cout << "-m\t--redis-min-idle\tredis连接池的最小空闲数" << std::endl;
            std::cout << "-M\t--redis-max-idle\tredis连接池的最大空闲数" << std::endl;
            std::cout << "-c\t--redis-count\t\tredis连接池的最大数量" << std::endl;
#endif // REDIS
            std::cout << "-h\t--help\t\t查看帮助" << std::endl;
            exit(0);
        default:
            std::cerr << "参数非法，输入-h或--help查看帮助" << std::endl;
            exit(1);
        }
    }

    if (!ipv4_ && !ipv6_) {
        ipv4_ = ipv6_ = true;
    }
    if (work_dir_.length() == 0) {
        char *buf = get_current_dir_name();
        work_dir_ = buf;
        free(buf);
    } else if (work_dir_.front() != '/') {
        std::cerr << "工作路径需为绝对路径" << std::endl;
        exit(1);
    }
    if (work_dir_.back() != '/') {
        work_dir_.push_back('/');
    }

    if (webapps_path_.length() == 0) {
        webapps_path_ = work_dir_ + "webapps/";
    } else {
        if (webapps_path_.front() != '/') {
            webapps_path_ = work_dir_ + webapps_path_;
        }
        if (webapps_path_.back() != '/') {
            webapps_path_.push_back('/');
        }
    }

    if (root_url_.length() == 0) {
        root_url_ = "/root/";
    } else {
        if (root_url_.front() != '/') {
            root_url_.insert(root_url_.begin(), '/');
        }
        if (root_url_.back() != '/') {
            root_url_.push_back('/');
        }
    }
}
