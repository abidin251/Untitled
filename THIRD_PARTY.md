# Third-party components and data

This project vendors the following code. Their licenses apply to those files.

## Dear ImGui 1.80

- Files: `deps/imgui/`
- License: MIT
- Source: https://github.com/ocornut/imgui/tree/v1.80
- Copyright (c) 2014-2021 Omar Cornut

Nexus requires addons to use exactly ImGui 1.80 and the shared Nexus ImGui
context. Do not upgrade this copy independently of Nexus.

## nlohmann/json 3.11.3

- File: `deps/nlohmann/json.hpp`
- License: MIT
- Source: https://github.com/nlohmann/json/releases/tag/v3.11.3
- Copyright (c) 2013-2022 Niels Lohmann

## Nexus addon API header

- File: `deps/nexus/Nexus.h`
- License: MIT
- Source: https://github.com/RaidcoreGG/RCGG-lib-nexus-api
- Copyright (c) Raidcore.GG

## Data sources

- **Guild Wars 2 API** — items, recipes, prices, currencies, account data and
  legendary armory. Operated by ArenaNet. https://api.guildwars2.com
  Use is subject to ArenaNet's terms for the Guild Wars 2 API.
- **gw2efficiency recipe API** — nested acquisition graphs including Mystic
  Forge, merchants, achievements and currencies. Operated by gw2efficiency.
  https://api.gw2efficiency.com
  The addon queries this service at runtime and caches responses locally; no
  dataset is redistributed with this repository.
- Item and currency icons are loaded from `render.guildwars2.com` by Nexus.

## Game assets and trademarks

Guild Wars 2, Guild Wars, ArenaNet, NCSOFT, the Interlocking NC Logo and all
associated logos and designs are trademarks or registered trademarks of NCSOFT
Corporation. All rights reserved. This project is unofficial and is not
affiliated with or endorsed by ArenaNet or NCSOFT.

The addon icon is an original asset created for this project and is covered by
the repository's MIT license.
