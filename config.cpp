#include <iostream>
#include <getopt.h>
#include <unistd.h>

#include "config.h"

Config::Config() {
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
    struct option argarr[] = {{"port", 1, nullptr, 'p'}, {"path", 1, nullptr, 'P'}, {"file", 1, nullptr, 'f'}, {"thread", 1 , nullptr, 't'}, {"level", 1, nullptr, 'l'}, {"async", 0, nullptr, 'a'}, {"so", 1, nullptr, 's'}, {"webapps", 1, nullptr, 'w'}, {"help", 0, nullptr, 'h'}, {nullptr, 0, nullptr, 0}};
    int index = 0, c = -1;
    while ((c = getopt_long(argc, argv, "p:P:f:t:l:as:w:h", argarr, &index)) >= 0) {
        switch(c) {
        case 'p':
            this->port = atoi(optarg);
            break;
        case 'P':
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
        case 'h':
            std::cout << "-p\t--port\t\t监听端口，默认8888" << std::endl;
            std::cout << "-P\t--path\t\t静态资源路径" << std::endl;
            std::cout << "-f\t--file\t\t主页，默认index.html" << std::endl;
            std::cout << "-t\t--thread\t线程数量，默认8" << std::endl;
            std::cout << "-l\t--level\t\t日志级别,1-error,2-warn,3-info,4-debug,其它关闭日志，默认3" << std::endl;
            std::cout << "-a\t--async\t\t开启异步日志，默认同步日志" << std::endl;
            std::cout << "-s\t--so\t\t动态库so文件的名称，默认main.so" << std::endl;
            std::cout << "-w\t--webapps\t动态库所在路径，默认为工作目录下的webapps目录" << std::endl;
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
