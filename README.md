# Cache Version

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that tells players' launchers
when to clear the WoW client's `Cache` folder.

The 3.3.5a client keeps what the server tells it about items, quests, creatures, game objects, NPC
text and pages in `Cache/WDB`, and reuses it between sessions. When the server changes one of those
(a reworked item, a renamed NPC, a fixed quest text), players keep seeing the old version until
they delete `Cache` by hand.

With this module:

- **At every startup the worldserver checksums the tables the client caches** (`item_template`,
  `quest_template` and friends, `creature_template`, `gameobject_template`, `npc_text`, `page_text`,
  plus their `_locale` tables) and writes the result to `cache-version.txt`.
- **The file goes next to mod-realm-config's `realm.conf`** (`RealmConfig.OutputDirectory`), so
  the portal serves it at `/realm/cache-version.txt` with no extra setup.
- **The Evermore launcher** ([wow-Portalkeeper](https://github.com/buildthehomelab/wow-Portalkeeper),
  0.5.4 and later) reads it and **clears each player's `Cache` once whenever it changes**, after its
  next realm check or right before ENTER REALM.
- **Nothing changes, nothing happens.** The file is only rewritten when the value changes, so a
  plain restart doesn't clear anyone's cache.
- **`CacheVersion.Salt`** forces a new value without touching the database, for the odd case where
  you want everyone's cache cleared anyway.

Patch changes don't need this: the launcher clears `Cache` itself whenever it installs, updates
or removes a patch.

## Patch Notes: Cache Version

Category: Launcher

- When we change items, quests, NPCs or other game data, the launcher now clears WoW's cache for
  you, so you see the new versions right away. No more deleting the Cache folder by hand.

## Requirements

- AzerothCore WotLK (tested against the mod-playerbots branch at `f19a187`).
- [mod-realm-config](https://github.com/Hisha/mod-realm-config) for the default output folder, or
  set `CacheVersion.OutputFile` yourself.
- The Evermore launcher 0.5.4 or later on the players' side. Without it the file is harmless.

## Installation

1. Clone into the server's `modules/` folder as `mod-cache-version` and rebuild:

   ```bash
   cd modules && git clone https://github.com/buildthehomelab/wow-mod-cache-version.git mod-cache-version
   ```

2. Copy `conf/mod_cache_version.conf.dist` to `mod_cache_version.conf` in your config folder.
   The defaults work as-is when mod-realm-config is installed.

On startup the log says either `client cache data unchanged (<value>)` or
`client cache data changed, wrote .../cache-version.txt (<value>)`.

## Configuration

| Option | Default | What it does |
| --- | --- | --- |
| `CacheVersion.Enable` | `1` | Master switch. |
| `CacheVersion.OutputFile` | `""` | Where to write the file. Empty = `cache-version.txt` in `RealmConfig.OutputDirectory`. |
| `CacheVersion.Tables` | the WDB tables | Comma-separated world tables to checksum. Missing tables count as "none". |
| `CacheVersion.Salt` | `""` | Mixed into the value; change it to force a cache clear. |

`CHECKSUM TABLE` reads each table once at startup; on a full world database that takes a second or
two.

## Troubleshooting

- **`no output file` in the log:** mod-realm-config isn't installed or `RealmConfig.OutputDirectory`
  is empty. Set `CacheVersion.OutputFile` to a path the portal serves.
- **`can't write` / `can't replace`:** the worldserver user can't write to that folder. It's the same
  folder mod-realm-config writes `realm.conf` to, so check its owner (UID 1000 in the Docker setup).
- **Players still see old data:** the launcher only checks after a realm refresh or right before
  launch. CHECK FILES in the launcher forces one. Players without the launcher still need to delete
  `Cache` by hand.

## Credits

Written for the Vaultrona / Evermore realm. The launcher side lives in
[wow-Portalkeeper](https://github.com/buildthehomelab/wow-Portalkeeper), a fork of
[Hisha/Portalkeeper](https://github.com/Hisha/Portalkeeper).

## License

MIT, see [LICENSE](LICENSE).
