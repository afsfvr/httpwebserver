#ifndef BLACKLIST_H_
#define BLACKLIST_H_

#include <shared_mutex>

class BlackList {
public:
    BlackList(const BlackList&) = delete;
    BlackList& operator=(const BlackList&) = delete;
    static BlackList* getInstance();
    void load();
    void save();
    void add(const std::string &ip, bool persist = true);
    void remove(const std::string &ip, bool persist = true);
    bool contains(const std::string &ip);
private:
    BlackList();

    std::unordered_set<std::string> black_list_;
    mutable std::shared_mutex mutex_;
    const std::string file_ = "black.txt";
};

#endif // BLACKLIST_H_
