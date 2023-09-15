#include <locale>
#include <csignal>

#include "webserver.h"
#include "config.h"
#include "log.h"

WebServer *webserver;
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
    webserver = new WebServer;
    webserver->eventLoop();

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
