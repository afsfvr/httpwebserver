#include "webserver.h"
#include "config.h"

#ifdef USE_REDIS
#include "redis_pool.h"
RedisPool *pool;
#endif

WebServer *webserver;
static struct termios termiosSettings;
std::string encoding;
void quit(int x);
void daemonize();
void hookFunction();
void initSpdlog();

int main(int argc, char *argv[]) {
    Config *config = Config::getInstance();
    config->parse(argc, argv);
    initSpdlog();
    SPDLOG_DEBUG("工作路径: {}", config->getWorkDirectory());
    if (chdir(config->getWorkDirectory().c_str()) == -1) {
        perror("切换工作目录失败");
        exit(1);
    }
    if (config->isDaemon()) {
        daemonize();
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
        SPDLOG_WARN("收到信号SIGINT,退出程序", x);
        break;
    case 3:
        SPDLOG_WARN("收到信号SIGQUIT,退出程序", x);
        break;
    case 15:
        SPDLOG_WARN("收到信号SIGTERM,退出程序", x);
        break;
    default:
        SPDLOG_WARN("收到信号{},退出程序", x);
        break;
    }
    webserver->stop();
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork出错");
        exit(1);
    }
    if (pid > 0) {
        printf("守护进程pid位：%d\n", pid);
        exit(0);
    }
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        perror("/dev/null open出错");
        exit(1);
    }

    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2) close(fd);

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

#ifdef NO_LOG
void initSpdlog() {}
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/details/log_msg.h>
class thread_name_flag: public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override {
        thread_local std::string thread_name_cache = [] {
            char name[16];
            pthread_getname_np(pthread_self(), name, sizeof(name));
            return std::string(name, strnlen(name, sizeof(name)));
        }();

        dest.append(thread_name_cache.data(), thread_name_cache.data() + thread_name_cache.size());
    }
    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<thread_name_flag>();
    }
};
void initSpdlog() {
    auto level = static_cast<spdlog::level::level_enum>(Config::getInstance()->getLogLevel());
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<thread_name_flag>('N');
    formatter->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%L] [%t:%N] [%g:%#] %v%$");

    // auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_formatter(std::move(formatter));
    console_sink->set_level(level);
    console_sink->set_color_mode(spdlog::color_mode::always);

    const std::string &file = Config::getInstance()->getLogFile();
    std::vector<spdlog::sink_ptr> sinks;
    if (! file.empty()) {
        auto file_formatter = std::make_unique<spdlog::pattern_formatter>();
        file_formatter->add_flag<thread_name_flag>('N');
        file_formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%L] [%t:%N] [%s:%#] %v");

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file,
            1024 * 1024 * 10,
            10,
            false
        );
        file_sink->set_formatter(std::move(file_formatter));
        file_sink->set_level(level);

        sinks = std::vector<spdlog::sink_ptr>{ console_sink, file_sink };
        SPDLOG_INFO(
            "控制台日志级别: {}, 文件日志级别: {}, 路径: {}",
            spdlog::level::to_string_view(console_sink->level()),
            spdlog::level::to_string_view(file_sink->level()),
            file_sink->filename()
        );
    } else {
        SPDLOG_INFO(
            "控制台日志级别: {}",
            spdlog::level::to_string_view(console_sink->level())
        );
        sinks = std::vector<spdlog::sink_ptr>{ console_sink };
    }
    auto logger = std::make_shared<spdlog::logger>(PROJECT_NAME, sinks.begin(), sinks.end());
    logger->set_level(level);
    logger->flush_on(spdlog::level::warn);
    spdlog::register_or_replace(logger);
    spdlog::set_default_logger(logger);

    SPDLOG_DEBUG("日志系统初始化完成");
}
#endif
