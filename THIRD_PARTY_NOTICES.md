# Third-Party Notices

This project is licensed under the **GNU General Public License v3.0** (see
[LICENSE](LICENSE)). It bundles or links the following third-party components.

## Bundled source

| Component | Upstream | License | Location |
| :--- | :--- | :--- | :--- |
| Weasel (小狼毫) | https://github.com/rime/weasel | GPLv3 | `3rdparty/weasel/` |
| Weasel upstream license text | — | GPLv3 | `3rdparty/weasel/LICENSE.txt` |
| WTL (Windows Template Library) | https://github.com/forgottenworks/wtl | Boost Software License 1.0 | `3rdparty/weasel/include/wtl/` |

Vendored upstream commit and module list:
[`3rdparty/weasel/UPSTREAM.md`](3rdparty/weasel/UPSTREAM.md).

## Linked libraries

| Component | Version | License |
| :--- | :--- | :--- |
| librime | 1.17.x | GPLv3 |
| Boost | 1.88.0 | Boost Software License 1.0 |
| Qt | 6.8.2 | LGPLv3 (dynamic linking) |
| Fluent-Qt | vendored snapshot | MIT |

## Runtime data (not source)

The distribution ships Rime data files that are fetched at build time, not
vendored into this repository:

| Data set | Upstream | License |
| :--- | :--- | :--- |
| rime-prelude | https://github.com/rime/rime-prelude | GPLv3 |
| rime-luna-pinyin | https://github.com/rime/rime-luna-pinyin | GPLv3 |

## Trademarks

"小狼毫 / Weasel" and "Rime" are trademarks of their respective owners. This
project is an independent downstream work and is not endorsed by the Rime
project.
