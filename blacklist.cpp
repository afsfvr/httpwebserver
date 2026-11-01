#include "blacklist.h"

BlackList *BlackList::getInstance() {
    // 使用 new 且不注册析构，规避静态对象生存期与 atexit 钩子的顺序问题
    static BlackList *instance = new BlackList;
    return instance;
}

BlackList::BlackList() {}

void BlackList::add(const std::string & ip, bool persist) {
    std::string trimmed = trim(ip);
    if (trimmed.length() == 0) return;
    if (trimmed == "localhost" || trimmed == "::1" || trimmed == "127.0.0.1") return;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto iter = black_list_.insert(trimmed);
        if (! iter.second) return; // 已存在，无需重复保存
    }
    if (persist) {
        save();
    }
}

void BlackList::remove(const std::string & ip, bool persist) {
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        int ret = black_list_.erase(ip);
        if (ret <= 0) return;
    }
    if (persist) {
        save();
    }
}

void BlackList::load() {
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        black_list_.clear();
    }
    std::ifstream fin(file_);
    if (!fin.is_open()) {
        SPDLOG_WARN("无法打开黑名单文件{}: {}", file_, std::strerror(errno));
        return;
    }

    std::string ip;
    while (std::getline(fin, ip)) {
        std::string trimmed = trim(ip);
        if (trimmed.length() != 0) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            black_list_.insert(trimmed);
        }
    }
    SPDLOG_DEBUG("加载黑名单完成，共计{}个ip", black_list_.size());
    fin.close();
}

void BlackList::save() {
    std::ofstream fout(file_, std::ofstream::out | std::ofstream::trunc);
    if (!fout.is_open()) {
        SPDLOG_WARN("无法写入黑名单文件{}: {}", file_, std::strerror(errno));
        return;
    }

    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto &ip : black_list_) {
            fout << ip << "\n";
        }
    }
    fout.close();
    SPDLOG_DEBUG("保存黑名单完成");
}

bool BlackList::contains(const std::string & ip) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return black_list_.find(ip) != black_list_.end();
}

std::string BlackList::trim(const std::string & str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
