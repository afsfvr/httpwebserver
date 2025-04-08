#include <locale>
#include <csignal>
#include <termios.h>
#include <fcntl.h>

#include "webserver.h"
#include "config.h"
#include "log.h"

#ifdef USE_REDIS
#include "redis_pool.h"
RedisPool *pool;
#endif

WebServer *webserver;
static struct termios termiosSettings;
std::string encoding;
void quit(int x);
void daemonize(const char *outFile="/dev/null");
void hookFunction();

int main(int argc, char *argv[]) {
    Config *config = Config::getInstance();
    config->parse(argc, argv);
    LOG_DEBUG("工作路径: %s", config->getWorkDirectory().c_str());
    if (chdir(config->getWorkDirectory().c_str()) == -1) {
        perror("切换工作目录失败");
        exit(1);
    }
    const std::string &daemon = config->getDaemon();
    if (daemon.length() != 0) {
        daemonize(daemon.c_str());
    }

    encoding = std::locale("").name();
    size_t i = encoding.find('.');
    if (i != std::string::npos) encoding = encoding.substr(i + 1);

    tcgetattr(fileno(stdin), &termiosSettings);
    termiosSettings.c_lflag &= ~ECHO;
    tcsetattr(fileno(stdin), TCSAFLUSH, &termiosSettings);
    atexit(hookFunction);

#ifdef USE_REDIS
    pool = nullptr;
    pool = new RedisPool(config->getRedisMinIdle(), config->getRedisMaxIdle(), config->getRedisMaxCount(), config->getRedisIp().c_str(), config->getRedisPort());
#endif
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, quit);
    signal(SIGQUIT, quit);
    signal(SIGTERM, quit);
    webserver = new WebServer;
    webserver->eventLoop();

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
        perror("/dev/null open出错");
        exit(1);
    }
    if (out < 0) {
        perror(std::string(outFile).append(" open出错").c_str());
        exit(1);
    }

    dup2(input, 0);
    dup2(out, 1);
    dup2(out, 2);
    if (input > 2) close(input);
    if (out > 2) close(out);

    setsid();
}

void hookFunction() {
    termiosSettings.c_lflag |= ECHO;
    tcsetattr(fileno(stdin), TCSANOW, &termiosSettings);

#ifdef USE_REDIS
    delete pool;
#endif
    delete webserver;
}
