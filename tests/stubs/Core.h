#pragma once
// External service adapters only; the production module is included unchanged.
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <openssl/sha.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
using uint8 = std::uint8_t;
namespace TestCore
{
inline std::unordered_map<std::string, std::string> options;
inline unsigned warnings = 0, errors = 0;
struct ConfigMgr
{
    template <class T> T GetOption(std::string const &key, T fallback, bool = true)
    {
        auto it = options.find(key);
        if (it == options.end())
            return fallback;
        if constexpr (std::is_same_v<T, std::string>)
            return it->second;
        else
            return it->second == "1";
    }
};
inline ConfigMgr config;
} // namespace TestCore
inline auto *sConfigMgr = &TestCore::config;
#define LOG_WARN(...) (++TestCore::warnings)
#define LOG_ERROR(...) (++TestCore::errors)
#define LOG_INFO(...) ((void)0)
struct Field
{
    std::optional<std::string> value;
    bool IsNull() const
    {
        return !value;
    }
    template <class T> T Get() const
    {
        return value.value();
    }
};
struct QueryRows
{
    std::array<Field, 2> fields;
    Field *Fetch()
    {
        return fields.data();
    }
};
using QueryResult = std::shared_ptr<QueryRows>;
struct Database
{
    std::unordered_map<std::string, std::optional<std::string>> rows;
    std::vector<std::string> queries;
    QueryResult Query(char const *format, std::string const &name)
    {
        if (std::string(format) != "CHECKSUM TABLE `{}`")
            throw std::runtime_error("Unexpected SQL format");
        queries.push_back(name);
        auto it = rows.find(name);
        if (it == rows.end())
            return {};
        auto result = std::make_shared<QueryRows>();
        result->fields[1].value = it->second;
        return result;
    }
};
inline Database WorldDatabase;
namespace Acore
{
inline std::vector<std::string_view> Tokenize(std::string_view text, char sep, bool keep)
{
    std::vector<std::string_view> result;
    std::size_t start = 0;
    for (;;)
    {
        auto end = text.find(sep, start);
        auto token = text.substr(start, end == text.npos ? end : end - start);
        if (keep || !token.empty())
            result.push_back(token);
        if (end == text.npos)
            return result;
        start = end + 1;
    }
}
namespace Crypto
{
struct SHA1
{
    static std::array<uint8, 20> GetDigestOf(uint8 const *text, std::size_t size)
    {
        std::array<uint8, 20> result{};
        ::SHA1(text, size, result.data());
        return result;
    }
};
} // namespace Crypto
} // namespace Acore
template <std::size_t N> std::string ByteArrayToHexStr(std::array<uint8, N> const &bytes)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (auto b : bytes)
    {
        result += hex[b >> 4];
        result += hex[b & 15];
    }
    return result;
}
enum WorldHook
{
    WORLDHOOK_ON_STARTUP,
    WORLDHOOK_ON_AFTER_CONFIG_LOAD
};
class WorldScript
{
  public:
    static inline WorldScript *registered = nullptr;
    std::vector<WorldHook> hooks;
    WorldScript(char const *, std::initializer_list<WorldHook> list) : hooks(list)
    {
        registered = this;
    }
    virtual ~WorldScript() = default;
    virtual void OnStartup()
    {
    }
    bool HasHook(WorldHook hook) const
    {
        return hooks.empty() || std::find(hooks.begin(), hooks.end(), hook) != hooks.end();
    }
};
