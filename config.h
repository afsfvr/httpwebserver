#ifndef CONFIG_H_
#define CONFIG_H_

#include <string>
#include <map>
#include <pthread.h>

class Config {
public:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    static Config* getInstance();
    void parse(int argc, char *argv[]);
    int getPort() const;
    const std::string& getRootPath() const;
    const std::string& getMainFile() const;
    int getThreadNum() const;
    const std::map<std::string, std::string>& getType() const;
    bool getAsyncWriteLog() const;
    int getLogLevel() const;
private:
    Config();
    int port;
    std::string root_path;
    std::string main_file;
    bool async_write_log;
    int log_level;
    int threadNum;
    std::map<std::string, std::string> type;
};

#endif
