#ifndef SESSION_H_
#define SESSION_H_

#include <map>
#include <string>

class Session {
    public:
        Session(uint64_t sessionId);
        uint64_t getId() const;
        bool addAttribute(const std::string& key, const std::string& value) const;
        bool setAttribute(const std::string& key, const std::string& value) const;
        int getMaxInactiveInterval() const;
        bool setMaxInactiveInterval(int interval) const;
        time_t getCreateTime() const;
        std::string getAttribute(const std::string& key) const;
        std::map<std::string, std::string> getAttributes() const;
        bool removeAttribute(const std::string &key) const;
        bool invalidate();
    private:
        uint64_t m_sessionId;
};

#endif
