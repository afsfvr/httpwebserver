#include <locale>
#include <csignal>
#include <fcntl.h>

#include "webserver.h"
#include "config.h"
#include "log.h"

#ifndef NO_REDIS
#include "redis_pool.h"
RedisPool *pool;
#endif

WebServer *webserver;
std::string encoding;
void quit(int x);
void daemonize(const char *outFile="/dev/null");

int main(int argc, char *argv[]) {
    /* daemonize("./libwebserver.log"); */
    encoding = std::locale("").name();
    size_t i = encoding.find('.');
    if (i != std::string::npos) encoding = encoding.substr(i + 1);
    Config *config = Config::getInstance();
    config->parse(argc, argv);;
#ifndef NO_REDIS
    pool = nullptr;
    pool = new RedisPool(config->getRedisMinIdle(), config->getRedisMaxIdle(), config->getRedisMaxCount(), config->getRedisIp().c_str(), config->getRedisPort());
#endif
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, quit);
    signal(SIGQUIT, quit);
    signal(SIGTERM, quit);
    webserver = new WebServer;
    webserver->eventLoop();

#ifndef NO_REDIS
    delete pool;
#endif
    delete webserver;
    return 0;
}

void quit(int x) {
    switch (x) {
    case 2:
        LOG_WARN("收到信号SIGINT,退出程序", x);
        break;
    case 3:
        LOG_WARN("收到信号SIGQUIT,退出程序", x);
        break;
    case 15:
        LOG_WARN("收到信号SIGTERM,退出程序", x);
        break;
    default:
        LOG_WARN("收到信号%d,退出程序", x);
        break;
    }
    webserver->stop();
}

void daemonize(const char *outFile) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork出错");
        exit(1);
    }
    if (pid > 0) {
        printf("守护进程pid位：%d\n", pid);
        exit(0);
    }
    int input = open("/dev/null", O_RDWR);
    int out = open(outFile, O_WRONLY | O_CREAT | O_TRUNC , 0664);
    if (input < 0) {
        perror("open出错");
        exit(1);
    }
    if (out < 0) {
        perror("open出错");
        exit(1);
    }

    dup2(input, 0);
    dup2(out, 1);
    dup2(out, 2);
    if (input > 2) close(input);
    if (out > 2) close(out);

    setsid();
}
