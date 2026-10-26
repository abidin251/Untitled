# Mystic Crafting for Nexus

A Nexus addon that plans Guild Wars 2 legendary and item crafting: a full acquisition tree, synchronized account materials, live Trading Post prices, a shopping list and per-item source choices.

This is a **clean-room port** of the Blish HUD [Mystic Crafting module](https://blishhud.com/modules/?module=MysticCrafting.Module) to the [Nexus](https://github.com/RaidcoreGG/Nexus) addon API. The original module is closed-source; this project does not contain or copy its code or data. It reimplements the documented feature set against public APIs.

![Addon icon](assets/icon.png)

## Features

| Area | What it does |
| --- | --- |
| Legendary catalog | 200+ legendary armory items, grouped by type, with search, type/ownership filters, tracked-only filter and sortable columns |
| Acquisition tree | Recursive requirement tree for recipes, Mystic Forge combinations, merchants, achievements, containers and currencies |
| Account sync | Material storage, bank, shared inventory, character bags, wallet and legendary armory (all optional) |
| Costs | Live Trading Post prices (instant-buy or buy-order basis), vendor coin costs, Mystic Forge inputs and currency costs |
| Buy vs. craft | Automatically buys a tradable component when that is cheaper, with a per-item override (automatic / always craft / always buy) |
| Crafting steps | Dependency-ordered, numbered actions for the open plan or every tracked plan: gather and buy ingredients first, craft the goal last, with amounts, sources and costs; clipboard export |
| Shopping list | Aggregated purchases for one plan or every tracked plan with a source column: Trading Post, merchant ingredients (Icy Runestones, Mystic Runestones, Obsidian Shards, ...) and achievement ingredients, each at the cheapest available price, plus currency requirements and clipboard export |
| Research notes | Optional costing of the research notes a plan still needs, at a configurable gold-per-note price |
| Item details | Icons, rarity, type, description, vendor value, Trading Post prices and chat-link copying |
| Progress | Remaining cost and "from scratch" cost with an owned-materials progress bar |
| Workflow | Tracked plans, persisted settings, an API key encrypted with Windows DPAPI, manual refresh and disk caches |
| Nexus integration | Main window, keybind, Quick Access shortcut, Escape-to-close, options page, alerts and logging |

### Parity notes

The Blish HUD module is closed-source, so feature parity is based on its public description and issue tracker. Differences worth knowing:

- Recipe data comes from the public gw2efficiency recipe API. It exposes one canonical acquisition path per item (with a note when multiple sources exist), not every selectable alternative. The original's "open the starter kit vs. use the Mystic Forge" chooser is therefore represented as **automatic / craft / buy**, not a list of every vendor or container variant.
- The catalog cost column is filled when a plan has been opened and is cached in `cache/plan-costs.json`. Costs are not precomputed for all 200+ legendaries on first launch, which keeps startup fast and avoids hammering the data provider.
- The UI is native Nexus/ImGui rather than a pixel copy of the Blish HUD layout.
- There is no localization layer yet; strings are English.
- The addon does not read game memory, hook the game or open the in-game preview window. It uses the public APIs only.

## Requirements

- Windows 10/11, 64-bit.
- Guild Wars 2 with Nexus installed.
- To build: CMake 3.20+, a C++17 compiler and the sources in `deps/` (ImGui 1.80, nlohmann/json 3.11.3 and the Nexus API header are vendored).
- Optional: a Guild Wars 2 API key with `account`, `inventories`, `characters`, `wallet` and `unlocks` permissions for account synchronization.

## Install (prebuilt)

1. Copy `MysticCrafting.dll` into `Guild Wars 2\addons\`.
2. Start the game. Open Nexus with `Ctrl+O`, go to **Addons**, find **Mystic Crafting** and click **Load**.
3. Click the Mystic Crafting Quick Access icon, or set a keybind for **Mystic Crafting: Toggle** in the addon's settings.
4. Open the **Account** tab, paste an API key and click **Save and synchronize**.

The addon creates its settings and cache under `Guild Wars 2\addons\MysticCrafting\`. Icons are embedded in the DLL; no loose assets are required.

## Build

Everything needed to compile is vendored. No package manager or network access is required for the build itself.

### Visual Studio 2022 (MSVC)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The DLL is written to `build\Release\MysticCrafting.dll`.

### Cross-compile from Linux with LLVM-MinGW

```bash
export LLVM_MINGW_ROOT=/opt/llvm-mingw
cmake -S . -B build-win \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-llvm-mingw.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
```

The runtime is linked statically; the resulting DLL only depends on Windows system libraries (`WINHTTP`, `CRYPT32`, UCRT).

### Tests

On Linux (or another non-Windows host) CMake builds two console test targets:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j
ctest --test-dir build-linux --output-on-failure
```

- `planner` covers recipe costs, ownership allocation, automatic buy/craft decisions, forced source preferences, fractional Mystic Forge yields, currencies, combined tracked plans sharing one ownership pool, crafting-step ordering and actions, JSON parsing and coin formatting.
- `ui_smoke` creates an ImGui context and renders every tab and the options page, catching table/ID/stack errors without a renderer. Set `MC_UI_DUMP_DIR=<dir>` to rasterize each tab to a PPM file, and `MC_UI_ONLY_MAIN=1` to leave the options page out of those previews.

The tests do not run Nexus or the game. See **Verification** below.

## Using the planner

- **Planner tab** — pick a legendary on the left. The right side shows its requirement tree. Click a column header to sort; use the search box, type filter and ownership filter to narrow the catalog.
- **Tree rows** — `Need`, `Owned`, `Missing`, `Source` and cumulative `Cost`. Expand arrows reveal sub-components. Hover a row for item details and prices.
- **Right-click a tree row** — force automatic/craft/buy for that item, copy the item name, or copy its chat link.
- **Refresh** — re-fetches the acquisition graph and prices for the current plan.
- **Track** — pins the plan to the tracked list and marks it in the catalog.
- **Shopping List tab** — aggregated purchases plus wallet/currency requirements and shortfalls. Every line names its source: Trading Post, a merchant (with the merchant and map in the tooltip) or an achievement, and merchant prices are shown in the currency the merchant asks for (coin, karma, spirit shards, ...). Enable **Combine all tracked plans** to merge every tracked plan into one list; owned materials and wallet balances are allocated across the plans in tracked order, so shared ingredients are only subtracted once. **Copy list** exports the current view as plain text.
- **Crafting Steps tab** — a flat, numbered task list for the same target as the shopping list (every tracked plan when combining is on): gather currency, buy vendor and tradable components and craft each intermediate in dependency order, ending with the goal. Coin is left out because it is already part of the gold totals. **Copy steps** exports the list.
- **Research notes** — research notes are a wallet currency, so the plan already reports how many it needs. Tick **Include research note crafting cost** in Settings (or the options page) to price the missing notes at the **gold per research note** value you enter; the default is 1s. Use the current price per note from [true-farming](https://www.true-farming.com/salvage/research-notes) if you want a live figure.
- **Account tab** — API key, synchronization status, permissions, data-source toggles and wallet balances.
- **Settings tab** — price basis, buy-vs-craft behavior, ownership subtraction, density, research note cost, tracked plans, data provider URL and cache controls.
- **Open item** — enter any item ID next to the catalog filters to build a plan for a non-legendary item.

### Price basis

- **Instant buy** (default) uses the lowest sell listing — what you pay to buy now.
- **Buy order** uses the highest buy order — what a buy order costs, assuming it fills.

The planner compares the buy cost against the full craft cost of the remaining quantity. Crafting uses the same account ownership, vendor and currency rules as the tree.

## Data sources and caching

| Data | Source | Cache |
| --- | --- | --- |
| Legendary list | `api.guildwars2.com/v2/legendaryarmory` | 1 day |
| Items, rarities, icons, chat links | `api.guildwars2.com/v2/items` | 7 days (only missing IDs are fetched) |
| Prices | `api.guildwars2.com/v2/commerce/prices` | 5 minutes |
| Currencies, wallet names | `api.guildwars2.com/v2/currencies` | 30 days |
| Account | GW2 API account endpoints | 5 minutes / manual refresh |
| Acquisition graph | `api.gw2efficiency.com/recipes?ids=<item>` | 7 days per item |
| Computed costs | local cache | until cleared |

Cached data is stored under `addons\MysticCrafting\cache`. The **Settings → Clear cache and reload** button removes it and refetches the catalog. If the recipe provider is unavailable, the last cached graph for an item is used and a warning is shown.

The gw2efficiency endpoint is a public, third-party service. It can change or be unavailable; the core catalog, prices and account data still come from ArenaNet's official API. The provider URL is configurable in Settings.

## Privacy and security

- The API key is sent only to `api.guildwars2.com` over HTTPS, using the `Authorization: Bearer` header.
- The key is encrypted with Windows DPAPI before being written to `settings.json`, so it is readable only by the same Windows user.
- Account caches contain item counts and wallet balances, never the key.
- No telemetry, analytics or data is sent anywhere else. Recipe requests contain only an item ID.
- The addon does not read game memory, hook game functions or inject input. Nexus flags are `AF_None`.

## Source layout

```
src/
  Addon.cpp        Nexus entry points, worker queue, data jobs and caching
  App.h            Application state and background service API
  Core.h/.cpp      Shared types, formatting and item labels
  Http.cpp/.h      WinHTTP GET client with timeouts and bearer auth
  Parse.cpp/.h     GW2 API / acquisition graph JSON parsing
  Planner.cpp/.h   Requirement tree, crafting steps, ownership, buy-vs-craft and cost engine
  Store.cpp/.h     Settings (DPAPI), caches and JSON persistence
  Ui.cpp/.h        ImGui catalog, plan tree, shopping list, account/settings/about
assets/
  icon.png         Quick Access icon (embedded as a PNG resource in the DLL)
  icon_hover.png
  resources.rc.in
tests/
  test_plan.cpp    Planner/parser unit tests
  ui_smoke.cpp     Headless ImGui render test
  stub/Nexus.h     Minimal Nexus API stub for the UI test
```

### How planning works

1. The selected item's acquisition graph is loaded from the provider and parsed into `RecipeNode`s.
2. All referenced item IDs are resolved through the GW2 API (metadata) and Trading Post (prices).
3. `BuildPlan` walks the graph, consuming owned items and currencies from a shared pool so shared ingredients are not double-counted.
4. Each recipe node is costed recursively. If the item is tradable and cheaper to buy, the node collapses into a Trading Post purchase (unless overridden). A merchant ingredient is treated as a purchase too, so the cheaper of the merchant price and the Trading Post listing wins; a merchant that charges karma or another currency costs no gold and keeps its price visible on the shopping line.
5. Currency components are accumulated separately; coin costs from vendors are included in the gold total. Merchant and achievement ingredients are listed on the shopping list with their source, and the merchant and achievement details are kept for the tooltip.
6. Research notes (wallet currency 61) are always reported; when the option is on, the notes still missing are priced at the configured cost per note and added to the remaining and from-scratch totals.
7. A second pass with an empty account produces the "from scratch" denominator for progress.
8. For the combined shopping list, `BuildCombinedPlan` runs a plan per tracked goal against one ownership pool, deducting what each goal used before costing the next, then merges the shopping lists, currency requirements and totals. It also keeps each goal's tree, so `BuildCraftingSteps` can list the steps of every tracked goal in turn.
9. The resulting immutable `Plan` objects are published to the render thread.

## Verification

What has been checked in this repository:

- **Build**: cross-compiled with LLVM-MinGW (Clang 18) for x86-64 Windows; `GetAddonDef` is exported and the DLL imports only Windows system libraries (no `_wassert`, no libc++ runtime).
- **Unit tests**: planner and parser tests pass, including merchant ingredients, achievement ingredients, the optional research note cost, the cheapest merchant-vs-Trading-Post choice and crafting steps for a combined plan.
- **UI smoke test**: every tab and the Nexus options page renders in a headless ImGui 1.80 context without assertions, with Trading Post, merchant, achievement and research note shopping lines.
- **Real data**: the parser/planner were exercised against live gw2efficiency graphs plus live GW2 item, price and currency data. Twilight (30704) produced 25 shopping lines including 100 Icy Runestones at 1g each from Rojan the Penitent and karma or spirit shard prices for the other merchant ingredients; Obsidian Heavy Breastplate (101521) produced 3,500 research notes priced at 1s each (35g) inside a 403g 40s 11c plan; a combined Twilight plus Obsidian plan listed 189 steps across both goals.

What has **not** been checked here: loading the DLL inside Guild Wars 2 with Nexus. This environment has no Windows game client, so in-game rendering, the keybind/Quick Access flow, WinHTTP access from the game process and DPAPI behavior on the target machine still need a manual pass.

## Publishing

- The addon signature is `0x4D435231`. Keep it stable. If you publish on [raidcore.gg](https://raidcore.gg/gw2/nexus), register that signature before release.
- For automatic updates, set `Provider = UP_GitHub` and `UpdateLink` to the repository URL in `GetAddonDef`, then publish GitHub releases whose tags are `1.0.0` style and attach `MysticCrafting.dll`.
- `scripts/package.sh` builds the DLL (if a cross toolchain is available) and creates a release folder and zip.

## Credits and licenses

This project is MIT licensed; see [LICENSE](LICENSE). Third-party components and data sources are listed in [THIRD_PARTY.md](THIRD_PARTY.md).

Guild Wars 2, ArenaNet, NCSOFT and all associated assets are trademarks of their respective owners. This addon is unofficial and not affiliated with ArenaNet or the Blish HUD team.
