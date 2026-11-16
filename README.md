# SIP-01 Search

A native search engine for the decentralized web, built for Omarchy.

SIP-01 (kind 39697) is a federated web index on Nostr: independent indexers
observe pages and publish observations to shared relays. Nothing is owned by
anybody. This app puts that index behind a search box that behaves like any
other desktop search engine — instant results, keyboard-driven, and following
the Omarchy theme live.

![SIP-01 Search](screenshot.png)

## What it does

- **Fetches the index.** Pulls every observation from the SIP-01 index relays
  and keeps them in a local corpus (about 5,000 documents today).
- **Ranks locally.** Every candidate is re-scored on the document's own text
  — term frequency, title weight, phrase matches — rather than trusting
  whatever a relay returned. A candidate that does not match the query is
  dropped, so the app never shows unrelated pages as answers.
- **Ranks agreement.** Independent indexers observing the same page is
  agreement, and the distinct-author count is the core ranking signal of the
  protocol. Those pages rise.
- **Answers instantly.** The corpus is cached on disk, so a query never waits
  on the network, and the app works offline.
- **Follows the theme.** The Omarchy accent and Hyprland's window rounding
  are read live, so the app looks like part of the desktop.
- **Answers agents too.** `sipsearch --headless <query>` returns the same
  ranking as JSON, so an agent and a person get the same answer.

## Query syntax

The app understands what a search engine is expected to understand. The
rules below are Dsearch's, so a query means the same thing in either.

**Text and phrases**

- `nostr relays` — loose terms, matched as whole words. A loose query is an
  implicit AND: both words must appear. `decentralized index` means a page
  about both, not a page about either.
- `"decentralized index"` — a quoted phrase, matched contiguously.
- `rock and roll` — lowercase `and` is an ordinary word, not an operator.

Common words carry no signal and are never required: `index of the pages`
looks for `index` and `pages`, not for the word `of`. But a query that
collapses to a single keyword still has to hit something else — `how to
build a widget` will not match every page about widgets.

**Boolean operators** — uppercase only, precedence `NOT` > `AND` > `OR`:

- `nostr AND relays`
- `nostr OR bitcoin`
- `nostr NOT directory`
- `(nostr OR nostrich) AND relays`

**Field filters** — evaluated against the document's metadata, not its text:

| Filter | Meaning |
|---|---|
| `site:github.com` | the host is that domain or a subdomain of it |
| `domain:github.com` | the host is exactly that domain |
| `title:relay` | the term appears in the title |
| `type:pdf` | the document type or mime tail |
| `lang:de` | the ISO 639-1 language tag |
| `tag:nostr` | an exact topic tag — `tag:nostr` does not match `nostr-tools` |
| `before:2026-01-01` | strictly before that day |
| `after:2026-01-01` | on or after that day |

`site:github.com` matches `gist.github.com` but never `evilgithub.com`.

Filters show up as chips under the search field. Click one to drop it.

**Instant answers** — some queries are answered outright instead of being
searched for: arithmetic (`2+2`, `(3+4)*5`, `2^10`, `15% of 80`), URLs,
NIP-19 entities and NIP-05 addresses.

## Using it

```sh
sipsearch                      # open the window
sipsearch nostr                # open the window already searching for "nostr"
sipsearch --headless nostr     # JSON on stdout, no window
sipsearch --headless nostr --limit 5
```

The headless form exits 1 when nothing matched, so a script can tell "no
answers" from "the command broke".

## Requirements

- `sip01-search` on `$PATH` — the CLI that owns the protocol. It wraps the
  audited `sip-01` and `nostr` skills' scripts, so this app never
  reimplements the wire format.
- Qt 6 (base, declarative, wayland).

```sh
hermes skills install SIP-01/sip-01-skill/
hermes skills install NostrDanish/nostr-skill/
```

## Build

```sh
bin/build      # qmake6 + make into build/sipsearch
bin/test       # the Qt Test binary, offscreen
bin/install    # makepkg -fsi, so it lands in the app launcher
```

## Desktop integration

| Surface | How |
|---|---|
| App launcher | `bin/install` installs the `.desktop` entry and icon |
| Keybinding | `Super+Shift+S` → `omarchy-launch-or-focus sipsearch uwsm-app -- sipsearch` |
| Omarchy menu | the `SIP-01 Search` row under the SIP-01 submenu |
| Browser | `sip01-register-search-engine` adds Dsearch as a browser search engine |

### As the browser's search engine

Dsearch publishes an OpenSearch descriptor at `https://dsearch.com/opensearch.xml`.
To make it the default engine in Brave or Chromium:

```sh
sip01-register-search-engine --default
```

The browser must be closed first — it rewrites its `Preferences` file on
exit, so an edit made while it runs is lost. The script says so rather than
silently losing the change.

## Keyboard

| Key | Action |
|---|---|
| `Ctrl+F` | focus the search field |
| `↓` `↑` | move through the results |
| `Enter` | open the selected page |
| `Ctrl+C` | copy the selected URL |
| `Ctrl+R` | refresh the index |
| `?` | show the shortcuts |
| `Q` | quit |

## Layout

```text
sipsearch.pro           qmake project
bin/build               build
bin/test                tests
bin/install             package and install
src/main.cpp            app setup
src/backend.{h,cpp}     the corpus, the ranking, the index refresh
src/ranker.{h,cpp}      term weighting and phrase matching
src/theme.{h,cpp}       follows the Omarchy accent and Hyprland rounding
src/translator.{h,cpp}  follows the Omarchy language
src/Main.qml            the window
src/i18n/en.json        English, for the plural forms of counted strings
pkgbuild/               PKGBUILD, .desktop entry and icon
tests/                  Qt Test cases
```

## Protocol

Everything protocol-shaped lives in one place: the `sip-01` and `nostr`
skills' scripts, reached through the `sip01-search` CLI. This app asks that
CLI for the corpus and does the ranking itself. It never holds a Nostr key
and never signs anything — reading the index needs no credentials.

The CLI verifies every observation against the SIP-01 spec §18 step 2 before
the app sees it: the `d` tag must match the normalized `u` tag, and the `x`
tag must match the content hash. Observations that fail are discarded and
counted, and the app shows the count in its status line and in its headless
JSON as `invalid_dropped`. The app does not re-check what the CLI already
checked — one implementation of the rule, not two.

## License

MIT.
