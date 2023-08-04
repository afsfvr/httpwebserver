#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "webserver.h"
#include "config.h"

int main(int argc, char *argv[]) {
    Config::getInstance()->parse(argc, argv);;

    WebServer webserver;
    webserver.eventLoop();

    return 0;
}
