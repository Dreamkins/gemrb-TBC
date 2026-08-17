# GemRB

[![GitHub build status](https://github.com/gemrb/gemrb/actions/workflows/builder.yml/badge.svg)](https://github.com/gemrb/gemrb/actions/workflows/builder.yml)
[![AppVeyor build status](https://ci.appveyor.com/api/projects/status/k5atpwnihjjiv993?svg=true)](https://ci.appveyor.com/project/lynxlynxlynx/gemrb)
[![Coverity Badge](https://scan.coverity.com/projects/288/badge.svg)](https://scan.coverity.com/projects/gemrb)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=gemrb_gemrb&metric=alert_status)](https://sonarcloud.io/dashboard?id=gemrb_gemrb)
[![CII Best Practices](https://bestpractices.coreinfrastructure.org/projects/3101/badge)](https://bestpractices.coreinfrastructure.org/projects/3101)

## Introduction

[GemRB](https://gemrb.org) (Game Engine Made with preRendered Background) is a portable open-source
reimplementation of the Infinity Engine that underpinned Baldur's Gate,
Icewind Dale and Planescape: Torment. It sports a cleaner design, greater
extensibility and several innovations.
Would you like to create a game like Baldur's Gate?

To try it out you either need some of the ORIGINAL game's data or you can
get a tiny sneak peek by running the included trivial game demo.

The original game data has to be installed if you want to see anything but
the included trivial demo. On non-windows systems either copy it over from
a windows install, use a compatible installer, WINE or extract it manually
from the CDs using the unshield tool.

Documentation can be found on the [website](https://gemrb.org/Documentation),
in `gemrb/docs/` and the [gemrb.6 man page](https://gemrb.org/Manpage.html).

If you want to help out, start by reading this
list of [options, tips and priorities](https://github.com/gemrb/gemrb/blob/master/CONTRIBUTING.md).

---

## Turn-Based Combat Mod

This fork adds a full turn-based combat mode inspired by D&D 5e while preserving the original AD&D 2e mechanics.

### Controls

| Key | Action |
|-----|--------|
| `Enter` | Toggle turn-based mode (enabled by default). If something goes wrong, you can continue the battle in real-time with active pause |
| `Space` | End current character's turn |
| `Mouse wheel` | Scroll the initiative panel |

### Initiative Panel

A horizontal panel at the top of the screen displays portraits of all combat participants. First to act is on the left.

**Slot Elements:**
- **Character portrait** — clickable for selection
- **Movement bar** (below portrait) — shows remaining movement distance
- **Green square** — main action available
- **Yellow square** — free action available
- **Line** — connects attacker and target during opportunity attack (when active)

Characters with multiple attacks per round appear multiple times in the list — each appearance corresponds to one attack phase. Phases are separated by a vertical line.

### Initiative

At the start of each round, turn order is determined:

```
Initiative = Weapon Speed + d10 - Dexterity bonus + Size modifier
```

The **lower** the result, the **earlier** the character acts.

| Factor | Modifier |
|--------|----------|
| Weapon Speed | +Speed |
| Dice roll | +1d10 |
| Dexterity bonus | -reaction bonus |
| Haste | -2 |
| Slow | +2 |
| Size Tiny | -2 |
| Size Small | -1 |
| Size Medium | 0 |
| Size Large | +1 |
| Size Huge | +2 |

### Action System

#### Main Action (green square)

One per attack phase. Used for:

| Action | Note |
|--------|------|
| Weapon attack | One attack per phase |
| Cast spell | Cancels all subsequent attack phases |
| Use scroll | Treated as spell |
| Item from inventory | Any item used outside quick slots |
| Disarm Trap | Thief skill |
| Pick Lock | Thief skill |
| Pick Pocket | Thief skill |
| Turn Undead | Cleric/Paladin ability |
| Stealth (non-thief) | For characters without thief levels |

#### Free Action (yellow square)

One per entire turn (all attack phases). If unused — carries over to the next phase.

| Action | Note |
|--------|------|
| Potion/item from quick slot | Except scrolls |
| Switch active weapon | Toggle between weapon sets |
| Stealth (thief) | For characters with thief levels |
| Exit stealth | Deactivate Stealth mode |

#### Movement

- Each character can move a distance based on their speed
- Movement bar below portrait shows remaining allowance
- Movement does not cost actions — you can move and attack in any order

**Movement Rules (D&D 5e inspired):**

| Rule | Description |
|------|-------------|
| Enemies block | Enemy characters are impassable obstacles |
| Allies passable | You can move through allied characters |
| Ally penalty | Moving through an ally costs extra movement |
| No stacking | Cannot end turn on an ally's tile — auto-repositions back |
| No bumping | Characters don't push each other in TBC mode |
| Precise paths | Optimal pathfinding algorithm (no shortcuts) |

**Feedback Messages:**

| Message | Meaning |
|---------|---------|
| "Can't reach!" | No valid path or insufficient movement points |
| "Can't act here!" | Cannot perform action while on ally's tile |

**Pre-validation:** Before moving to attack/cast, the game checks if the path exists and movement is sufficient. If not — action is cancelled immediately without wasting movement.

### Attack Phases

Characters with multiple attacks per round receive additional phases:

- **Phase 0** — main phase, available to all
- **Phases 1-5** — additional attacks

**Notes:**
- Each phase grants one main action
- Free action — one for all phases, carries over if unused
- Long-casting spells can only be cast in **Phase 0** and they **cancel all subsequent phases**

### Opportunity Attacks

When a character **leaves the reach** of an enemy with a melee weapon:

1. Movement is interrupted
2. All enemies within striking range get a chance to attack
3. A line appears on the panel connecting attacker and target
4. Enemies attack in sequence
5. After opportunity attacks complete, turn returns to the original character

**Trigger conditions:**
- Enemy has a melee weapon
- Enemy has an unused attack
- Enemy can see the target
- Enemy is able to attack (not stunned, asleep, etc.)

### Modal Abilities

| Ability | Activation | Deactivation |
|---------|------------|--------------|
| Stealth (thief) | Yellow action | Yellow action |
| Stealth (others) | Green action | Yellow action |
| Detect Traps | Green action | Yellow action |
| Bard Song | Green action | Yellow action |
| Turn Undead | Green action | Yellow action |

---

## Supported platforms

Architectures and platforms that successfully run or ran GemRB:
* Linux x86, x86-64, ppc, mips (s390x builds, but no running info)
* FreeBSD, OpenBSD, NetBSD
* MS Windows
* various Macintosh systems (even pre x86)
* some smart phones (Symbian, Android, other Linux-based, iOS)
* some consoles (OpenPandora, Dingoo)
* some exotic OSes (ReactOS, SyllableOS, Haiku, AmigaOS, AmberElec, ArkOS, UnofficialOS)

If you have something to add to the list or if an entry doesn't work any more, do let us know!

## Requirements

See the INSTALL [file](https://github.com/gemrb/gemrb/blob/master/INSTALL).

## Contacts

There are several ways you can get in touch:
* [Homepage](https://gemrb.org)
* [GemRB forum](https://www.gibberlings3.net/forums/forum/91-gemrb/)
* [IRC channel](https://web.libera.chat/#GemRB), #GemRB on the Libera.Chat IRC network
* [Discord channel](https://discord.gg/64rEVAk) (Gibberlings3 server)
* [Bug tracker](https://github.com/gemrb/gemrb/issues/new/choose)


## Useful links

Original engine research and data manipulation software:
* [IESDP](https://gibberlings3.github.io/iesdp/), documentation for the Infinity Engine file formats and more
* [Near Infinity](https://github.com/NearInfinityBrowser/NearInfinity/wiki), Java viewer and editor for data files
* [DLTCEP](https://www.gibberlings3.net/forums/forum/137-dltcep/), MS Windows viewer and editor for data files
* [iesh](https://github.com/gemrb/iesh), IE python library and shell (for exploring data files)

Tools that can help with data installation:
* [WINE](https://www.winehq.org), Open Source implementation of the Windows API, useful for installing the games
* [Unshield](http://synce.sourceforge.net/synce/unshield.php), extractor for .CAB files created by InstallShield
