---
name: sipsearch
description: Use the native SIP-01 Search desktop app for the decentralized web index. Covers querying the local corpus, refreshing it from the index relays, ranking and ranking signals, the Omarchy integration surfaces, and the browser search-engine registration. Do not use for a one-off shell search — use the sip01-search CLI for that.
---

# SIP-01 Search

A native search engine over the SIP-01 web index (Nostr kind 39697), built
for Omarchy.

## The one job

Search the decentralized web index. The app fetches every observation from
the SIP-01 index relays, keeps them in a local corpus, and ranks them
locally so every query answers instantly and offline.

## Where the pieces live

| Piece | Path |
|---|---|
| App source | `~/Work/sipsearch/` |
| Built binary | `~/.local/bin/sipsearch` |
| Corpus cache | `~/.local/share/sipsearch/corpus.json` |
| Protocol CLI | `~/.local/bin/sip01-search` |
| Browser registration | `~/.local/bin/sip01-register-search-engine` |
| Omarchy menu | `~/.config/omarchy/extensions/omarchy-menu.jsonc` |
| Keybinding | `~/.config/hypr/bindings.lua` |

## Ranking

Ranking is local, always. The relay's answer is a candidate, not a verdict:
every candidate is re-scored on the document's own text, and a candidate that
does not match the query is dropped outright. Showing unrelated pages as
answers is worse than showing nothing.

The signals, in order of weight:

1. **Term frequency**, damped logarithmically, so one keyword-stuffed page
   cannot dominate the result set.
2. **Title hits**, worth more than body hits.
3. **Quoted phrases**, matched as a contiguous run of words; a phrase that is
   present scores strongly, and a phrase that is absent scores nothing.
4. **Independent indexers.** Distinct authors observing the same page is
   agreement, and it is the core ranking signal of the protocol. Those pages
   rise. The app shows the count as an `N×` badge.
5. **Length normalization**, so a long page does not outrank a focused one
   just by having more words.

## Query syntax

- `nostr` — loose terms, matched as whole words.
- `"decentralized index"` — a quoted phrase, matched contiguously.
- Mixed terms and phrases are allowed in one query.

**Boolean** — uppercase only, precedence `NOT` > `AND` > `OR`:

- `nostr AND relays`, `nostr OR bitcoin`, `nostr NOT directory`
- `(nostr OR nostrich) AND relays`
- Lowercase `and` stays an ordinary word, so `rock and roll` is not mangled.

**Filters** — evaluated against the document's metadata, never its text:

| Filter | Meaning |
|---|---|
| `site:github.com` | the host or a subdomain of it |
| `domain:github.com` | the host exactly |
| `title:relay` | the term is in the title |
| `type:pdf` | the document type or mime tail |
| `lang:de` | the ISO 639-1 language tag |
| `tag:nostr` | an exact topic tag |
| `before:2026-01-01` / `after:2026-01-01` | date bounds |

`site:` matches a subdomain but never a lookalike host. Filters show as
chips under the search field; clicking one drops it.

**Instant answers** — arithmetic, URLs, NIP-19 and NIP-05 are answered
outright. The answer is in the headless JSON too, under `instant_answer`.

## Asking the app

```sh
sipsearch --headless nostr                  # JSON, the window's own ranking
sipsearch --headless nostr --limit 5
sipsearch --headless "nostr relays"         # phrase
sipsearch nostr                             # open the window on a query
```

The headless form is the same code path the window uses, so an agent and a
person get the same ranking. It exits 1 when nothing matched.

Use `sip01-search --json <query>` for a live relay query instead: that one
hits the network, while the app answers from its cached corpus.

## Refreshing the index

```sh
sip01-search --dump --timeout 60
```

Prints the whole corpus as JSON. The app shells out to this; it is the only
path to the network, and it is where the protocol lives — the app never
reimplements the wire format.

The index is community-run, so it grows slowly and some pages are missing.
That is the protocol, not a bug.

## Browser integration

Dsearch publishes an OpenSearch descriptor at `https://dsearch.com/opensearch.xml`.

```sh
sip01-register-search-engine --default
```

Writes the engine into the browser profile's `Preferences`, the same shape
the browser's own settings page writes. **The browser must be closed first** —
it rewrites `Preferences` on exit, so an edit made while it runs is lost.
The script refuses rather than losing the change silently.

## Building

```sh
cd ~/Work/sipsearch
bin/build    # qmake6 + make into build/sipsearch
bin/test     # Qt Test binary, offscreen
bin/install  # makepkg -fsi, so it lands in the app launcher
```

## Signing boundary

The app reads the index and never writes to it. It holds no Nostr key and
signs nothing, because reading a public index needs no credentials. Publishing
observations still goes through a dedicated signer, never through this app.

## Pitfalls

- `qsTr` is not defined in this Qt build's headers; use `tr()` in C++.
- QProcess buffers stdout. A corpus dump is several megabytes, more than a
  pipe buffer holds, so drain it in `readyReadStandardOutput` or the child
  blocks writing and never finishes.
- The ranker must return zero when nothing matched. An indexer-agreement
  bonus applied to a non-matching document turns unrelated pages into results.
