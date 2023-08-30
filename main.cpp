#include <iostream>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <memory>

#include "webserver.h"
#include "config.h"
#include "log.h"

std::unique_ptr<WebServer> p(nullptr);
std::string encoding;
void quit(int x);

int main(int argc, char *argv[]) {
    encoding = std::locale("").name();
    size_t i = encoding.find('.');
    if (i != std::string::npos) encoding = encoding.substr(i + 1);
    Config::getInstance()->parse(argc, argv);;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, quit);
    signal(SIGQUIT, quit);
    signal(SIGTERM, quit);
    p.reset(new WebServer());
    p->eventLoop();

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
    p->stop();
}
