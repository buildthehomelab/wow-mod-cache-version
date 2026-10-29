#include "Core.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifndef CACHE_VERSION_SOURCE
#define CACHE_VERSION_SOURCE "../src/CacheVersion.cpp"
#endif
#include CACHE_VERSION_SOURCE
void Addmod_cache_versionScripts();
unsigned checks = 0;
void Check(bool ok, char const *name)
{
    ++checks;
    if (!ok)
    {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
}
std::string Read(std::filesystem::path const &p)
{
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), {}};
}
void Write(std::filesystem::path const &p, std::string const &text)
{
    std::ofstream f(p);
    f << text;
}
void Reset()
{
    TestCore::options.clear();
    TestCore::warnings = TestCore::errors = 0;
    WorldDatabase.rows.clear();
    WorldDatabase.queries.clear();
}
struct Fixture
{
    std::filesystem::path root;
    Fixture()
    {
        auto pattern = (std::filesystem::temp_directory_path() / "cache-version-tests-XXXXXX").string();
        auto *result = ::mkdtemp(pattern.data());
        if (!result)
            throw std::runtime_error("mkdtemp failed");
        root = result;
    }
    ~Fixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
int main()
{
    Fixture fixture;
    auto const target = fixture.root / "cache-version.txt";
    for (auto const &valid : std::vector<std::string>{"a", "0", "_", "item_template", std::string(64, 'x')})
        Check(IsTableName(valid), "valid identifier");
    for (auto const &invalid : std::vector<std::string>{"", std::string(65, 'x'), "a b", "a-b", "a`",
                                                        "a;DROP", "a\n", std::string(1, char(0xff))})
        Check(!IsTableName(invalid), "invalid identifier");
    Reset();
    Check(OutputFile().empty(), "no output configured");
    TestCore::options["RealmConfig.OutputDirectory"] = fixture.root.string();
    Check(OutputFile() == target, "directory fallback");
    TestCore::options["CacheVersion.OutputFile"] = (fixture.root / "explicit").string();
    Check(OutputFile() == fixture.root / "explicit", "explicit output precedence");
    Reset();
    TestCore::options["CacheVersion.Tables"] = " item_template ,\tquest_template\t,,bad-name, `evil`,   ";
    WorldDatabase.rows["item_template"] = "123";
    WorldDatabase.rows["quest_template"] = std::nullopt;
    Check(ComputeVersion() == "D75F2839C3A1881782D3BC6F12B9763C0A7520F7",
          "trimmed identifiers and NULL checksum digest");
    Check(WorldDatabase.queries == std::vector<std::string>{"item_template", "quest_template"},
          "invalid tables never queried");
    Check(TestCore::warnings == 3, "invalid table warnings");
    WorldDatabase.rows.erase("quest_template");
    Check(ComputeVersion() == "D75F2839C3A1881782D3BC6F12B9763C0A7520F7",
          "missing and NULL checksum share none marker");
    TestCore::options["CacheVersion.Salt"] = "force";
    Check(ComputeVersion() == "5C18342B97BF218618948F449A82ED7FB6B0B854", "salt changes digest");
    WorldDatabase.rows["quest_template"] = "0";
    Check(ComputeVersion() == "3B811424FE1BC6DF3D356366EA615086EBA79BFF", "zero checksum is data");
    TestCore::options["CacheVersion.Tables"] = "";
    Check(ComputeVersion() == "3E4E03173F082C970904EC7779553001AEB46CC5", "empty table list hashes salt");
    Reset();
    Check(ComputeVersion().size() == 40, "default digest size");
    Check(WorldDatabase.queries ==
              std::vector<std::string>{
                  "item_template", "item_template_locale", "quest_template", "quest_template_addon",
                  "quest_template_locale", "quest_offer_reward", "quest_offer_reward_locale",
                  "quest_request_items", "quest_request_items_locale", "creature_template",
                  "creature_template_locale", "gameobject_template", "gameobject_template_locale", "npc_text",
                  "npc_text_locale", "page_text", "page_text_locale"},
          "default table contract");
    Reset();
    WriteVersion();
    Check(WorldDatabase.queries.empty() && TestCore::warnings == 1, "no path avoids database work");
    TestCore::options["CacheVersion.OutputFile"] = target.string();
    TestCore::options["CacheVersion.Enable"] = "0";
    WriteVersion();
    Check(!std::filesystem::exists(target) && WorldDatabase.queries.empty(), "disabled avoids all work");
    TestCore::options.erase("CacheVersion.Enable");
    TestCore::options["CacheVersion.Tables"] = "item_template";
    WorldDatabase.rows["item_template"] = "123";
    Addmod_cache_versionScripts();
    auto *script = WorldScript::registered;
    Check(script != nullptr, "loader registration");
    Check(script->HasHook(WORLDHOOK_ON_STARTUP), "startup subscription");
    if (script->HasHook(WORLDHOOK_ON_STARTUP))
        script->OnStartup();
    auto const first = Read(target);
    Check(first == "642414881A4E2FF3DF92EAD19544AC4492762DCF\n", "startup writes exact version and newline");
    Check(!std::filesystem::exists(target.string() + ".tmp"), "successful rename consumes temporary");
    auto const oldTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    std::filesystem::last_write_time(target, oldTime);
    WriteVersion();
    Check(std::filesystem::last_write_time(target) == oldTime, "unchanged version is not rewritten");
    TestCore::options["CacheVersion.Salt"] = "force";
    WriteVersion();
    Check(Read(target) == "E061F0891B827000E5337C0F55F31FE759D73730\n", "salt rewrites existing file");
    WorldDatabase.rows["item_template"] = "456";
    WriteVersion();
    Check(Read(target) == "A6B4748CF190CA5D53ABFDF31DFCF2493270C835\n", "database change rewrites file");
    // A stale temporary must be truncated, not appended.
    Write(target.string() + ".tmp", std::string(500, 'x'));
    Write(target, "old\n");
    WriteVersion();
    Check(Read(target) == "A6B4748CF190CA5D53ABFDF31DFCF2493270C835\n", "stale temporary truncation");
    // Missing parent fails opening without touching any unrelated valid file.
    TestCore::options["CacheVersion.OutputFile"] = (fixture.root / "missing" / "version").string();
    WriteVersion();
    Check(TestCore::errors == 1, "open failure logged");
    Check(Read(target) == "A6B4748CF190CA5D53ABFDF31DFCF2493270C835\n",
          "open failure preserves previous file");
    // A nonempty directory reliably makes rename fail, even when tests run as root.
    auto const blocked = fixture.root / "blocked";
    std::filesystem::create_directory(blocked);
    Write(blocked / "keep", "keep");
    TestCore::options["CacheVersion.OutputFile"] = blocked.string();
    WriteVersion();
    Check(TestCore::errors == 2, "rename failure logged");
    Check(Read(blocked / "keep") == "keep", "rename failure preserves destination");
    Check(!std::filesystem::exists(blocked.string() + ".tmp"), "rename failure cleans temporary");
    delete script;
    WorldScript::registered = nullptr;
    std::cout << checks << " checks passed\n";
}
