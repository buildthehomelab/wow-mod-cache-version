/*
 * mod-cache-version
 *
 * Tells players' launchers when to clear the WoW client's Cache folder.
 *
 * The 3.3.5a client keeps what the server tells it about items, quests, creatures, game objects,
 * NPC text and pages in Cache/WDB, and reuses it across sessions. After the server changes one of
 * those (a reworked item, a renamed NPC, a quest text fix) players keep seeing the old version
 * until they delete Cache by hand.
 *
 * At startup this module checksums the world tables the client caches and writes the result to
 * cache-version.txt next to mod-realm-config's realm.conf (RealmConfig.OutputDirectory), so the
 * web server that serves realm.conf serves it too. Portalkeeper (the buildthehomelab fork) reads
 * it and clears Cache once whenever it changes; any other launcher or site can read it as well. So: change the data, restart the worldserver as
 * usual, and every player's stale cache goes away by itself.
 *
 * The file is only rewritten when the value changes. CacheVersion.Salt forces a new value without
 * touching the database.
 *
 * Released under the MIT License.
 */

#include "Config.h"
#include "CryptoHash.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "Util.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace
{
    // The world tables behind the client's WDB caches (itemcache, questcache, creaturecache,
    // gameobjectcache, npccache, pagetextcache) and their translations.
    constexpr char const* DEFAULT_TABLES =
        "item_template,item_template_locale,"
        "quest_template,quest_template_addon,quest_template_locale,quest_offer_reward,quest_offer_reward_locale,"
        "quest_request_items,quest_request_items_locale,"
        "creature_template,creature_template_locale,"
        "gameobject_template,gameobject_template_locale,"
        "npc_text,npc_text_locale,page_text,page_text_locale";

    bool IsTableName(std::string_view name)
    {
        if (name.empty() || name.size() > 64)
            return false;
        for (char c : name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                return false;
        return true;
    }

    std::filesystem::path OutputFile()
    {
        std::string file = sConfigMgr->GetOption<std::string>("CacheVersion.OutputFile", "", false);
        if (!file.empty())
            return file;
        std::string directory = sConfigMgr->GetOption<std::string>("RealmConfig.OutputDirectory", "", false);
        if (directory.empty())
            return {};
        return std::filesystem::path(directory) / "cache-version.txt";
    }

    // CHECKSUM TABLE for every configured table. A missing table (an older database without a
    // locale table, say) is part of the input as "none", so it doesn't break the value.
    std::string ComputeVersion()
    {
        std::ostringstream input;
        std::string const tables = sConfigMgr->GetOption<std::string>("CacheVersion.Tables", DEFAULT_TABLES);
        for (std::string_view table : Acore::Tokenize(tables, ',', false))
        {
            std::string name(table);
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            if (!IsTableName(name))
            {
                LOG_WARN("module", "mod-cache-version: skipping invalid table name '{}' in CacheVersion.Tables", name);
                continue;
            }

            std::string checksum = "none";
            if (QueryResult result = WorldDatabase.Query("CHECKSUM TABLE `{}`", name))
            {
                Field* fields = result->Fetch();
                if (!fields[1].IsNull())
                    checksum = fields[1].Get<std::string>();
            }
            input << name << '=' << checksum << ';';
        }
        input << "salt=" << sConfigMgr->GetOption<std::string>("CacheVersion.Salt", "", false);

        std::string const text = input.str();
        auto digest = Acore::Crypto::SHA1::GetDigestOf(reinterpret_cast<uint8 const*>(text.data()), text.size());
        return ByteArrayToHexStr(digest);
    }

    void WriteVersion()
    {
        if (!sConfigMgr->GetOption<bool>("CacheVersion.Enable", true))
            return;

        std::filesystem::path const path = OutputFile();
        if (path.empty())
        {
            LOG_WARN("module", "mod-cache-version: no output file (set CacheVersion.OutputFile or RealmConfig.OutputDirectory); cache-version.txt not written");
            return;
        }

        std::string const version = ComputeVersion();

        std::string current;
        if (std::ifstream existing{ path })
            std::getline(existing, current);
        if (current == version)
        {
            LOG_INFO("module", "mod-cache-version: client cache data unchanged ({})", version);
            return;
        }

        // Write next to the target and rename, so the portal never serves a half-written file.
        std::filesystem::path const temp = path.string() + ".tmp";
        {
            std::ofstream out{ temp, std::ios::trunc };
            if (!out)
            {
                LOG_ERROR("module", "mod-cache-version: can't write {}", temp.string());
                return;
            }
            out << version << '\n';
        }
        std::error_code error;
        std::filesystem::rename(temp, path, error);
        if (error)
        {
            LOG_ERROR("module", "mod-cache-version: can't replace {}: {}", path.string(), error.message());
            std::filesystem::remove(temp, error);
            return;
        }
        LOG_INFO("module", "mod-cache-version: client cache data changed, wrote {} ({})", path.string(), version);
    }
}

class CacheVersionWorldScript : public WorldScript
{
public:
    CacheVersionWorldScript() : WorldScript("CacheVersionWorldScript", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        WriteVersion();
    }
};

void AddCacheVersionScripts()
{
    new CacheVersionWorldScript();
}
