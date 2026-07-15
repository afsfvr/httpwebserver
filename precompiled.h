#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <variant>
#include <csignal>
#include <locale>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <string>
#include <set>
#include <map>
#include <unordered_set>
#include <vector>
#include <ctime>
#include <stdexcept>
#include <optional>
#include <functional>
#include <algorithm>

#include <pthread.h>
#include <termios.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <netinet/ip.h>

#ifdef NO_LOG
#define SPDLOG_TRACE(...) (void)0
#define SPDLOG_DEBUG(...) (void)0
#define SPDLOG_INFO(...) (void)0
#define SPDLOG_WARN(...) (void)0
#define SPDLOG_ERROR(...) (void)0
#define SPDLOG_CRITICAL(...) (void)0
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ranges.h>
#endif
