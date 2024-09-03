#ifndef COOKIE_H_
#define COOKIE_H_

#include <string>

class Cookie {
    public:
        enum SameSite {
            Null,
            Strict,
            Lax,
            None,
        };
        Cookie(const std::string& name, const std::string& value);
        bool operator<(const Cookie& cookie) const;
        bool operator==(const Cookie& cookie) const;
        std::string to_string() const;
        Cookie& setName(const std::string& name);
        Cookie& setValue(const std::string& value);
        Cookie& setDomain(const std::string& domain);
        Cookie& setExpires(time_t expires);
        Cookie& setHttpOnly(bool httpOnly);
        Cookie& setMaxAge(int maxAge);
        Cookie& setPartinioned(bool partitioned);
        Cookie& setPath(const std::string& path);
        Cookie& setSameSite(SameSite site);
        Cookie& setSecure(bool secure);

        const inline std::string& name() const { return m_name; }
        const inline std::string& value() const { return m_value; }
        const inline std::string& domain() const { return m_domain; }
        const inline time_t expires() const { return m_expires; }
        inline bool httpOnly() const { return m_http_only; }
        inline int maxAge() const { return m_max_age; }
        inline bool partitioned() const { return m_partitioned; }
        const inline std::string& path() const { return m_path; }
        Cookie::SameSite sameSite() const;
        inline bool secure() const { return m_secure; }
    private:
        std::string m_name;
        std::string m_value;
        std::string m_domain;
        time_t m_expires;
        bool m_http_only;
        int m_max_age;
        bool m_partitioned;
        std::string m_path;
        std::string m_same_site;
        bool m_secure;
};

#endif
