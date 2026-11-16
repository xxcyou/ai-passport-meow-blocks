<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Cat Cube

An offline deduction puzzle for the FoloToy AI Passport (ESP32-C3, 240x320
display, three ADC buttons). Cats are hidden on a colored grid; the player
crosses out impossible cells and confirms the cats they have deduced. Every
level has exactly one solution, so the answer can be reasoned out instead of
guessed.

## Rules

1. Any row and any column holds at most one cat.
2. A four-connected same-color region holds at most one cat. Region count equals
   the cat target, so each region holds exactly one cat.
3. Two cats may not touch, including all four diagonals.
4. Each level has exactly one valid placement, verified at generation time.

The cat target per level is fixed by the level table. A level is cleared when
every hidden cat has been confirmed and no wrong cell has been confirmed as a
cat.

## Controls

The device has three buttons: `UP`, `DOWN`, `OK` (short, double, long).

| Input | Effect |
| --- | --- |
| `DOWN` | Move the cursor down one row; from the last row it wraps to the top |
| `UP` | Move the cursor right one column; from the last column it wraps to the left |
| Hold `DOWN` / `UP` | Fast repeat after 300 ms (one step per 90 ms) |
| `OK` short | Toggle an "excluded" cross on the cursor cell; press again to clear |
| `OK` double | Confirm the cursor cell is a cat; double again to take it back |
| `OK` long | Open the menu. It has two pages: main (resume, restart, hint, settings, rules, level list) and settings (auto-exclude, volume, mute, reset progress, back). The clock and any level time limit pause while a menu, the volume page or the tutorial is open. |
| `UP` / `DOWN` in the volume page | Volume +10 / -10 (long `OK` goes back to the menu) |

Crosses are notes. Confirming is the commitment: a confirmed cell that is not a
cat leaves a **permanent red cross** on that cell (it cannot be cancelled or
confirmed again), shakes it briefly, plays a short error blip and costs one of
the two hearts. At zero hearts the level restarts. A cell
that the rules already rule out (same row, column, region, or adjacent to a
confirmed cat) cannot be confirmed at all, so the found counter can never exceed
the target.

### Auto-exclude assist

Confirming a cat also crosses out every cell that cat rules out: its row, its
column, its eight neighbours, and **every other cell of its whole color region**
(the region rule is applied in full, both for a manual confirm and for the hint).
Auto crosses use a dark tone that stays readable on all fourteen tile colors. Auto crosses are drawn
lighter than player-made crosses, and they are tracked per cell as a "how many
confirmed cats exclude me" counter: undoing a cat withdraws exactly the crosses
that cat contributed, and a hand-drawn cross is never removed by that.

## HUD, timer and stars

The play screen shows the level number, two hearts, the battery percentage in
the top-right corner, and a second line with the board size, the found/target
counter and the timer (remaining time on timed levels, red in the last 30 s).

Levels from 150 on are timed: 400 s up to level 199, 300 s up to 249, 240 s up
to 299 and 200 s beyond. Running out of time ends the level the same way as
running out of hearts.

Clearing a level plays a short sequence: every cat head scales up to 1.5x,
rotates, then the progress bar at the bottom fills one cat at a time, and the
result panel appears with a star rating. Stars are three, in half-star steps:
one heart lost costs half a star, and finishing well past par (4 s + 5 s per
cat) costs another half to a full star. The panel offers the next level or a
retry.

## Screens

- **Level list** — only cleared levels and the current one are listed; levels
  beyond the current one are not shown at all. Cleared levels are green, the
  current one blue-grey, and the selection has an amber frame.
- **Play** — level number, hearts, timer, board size, found/target, the
  self-drawn board, and a two-line control hint.
- **Tutorial** — levels 1-5 open with an illustrated rule card: three 4x4 mini
  boards show the row/column rule, the region rule and the adjacency rule, plus
  one line for the buttons. It pauses the clock and never appears from level 6 on.
- **Menu / rules / win / fail** — overlays on the play screen.

Cells are drawn as slightly raised tiles: a dark gap color under the board, each
tile inset with rounded corners, a lighter top edge and a darker bottom edge.
That is shading, not 3D geometry. The fourteen region colors are picked by
farthest-point sampling in HSV space, so the smallest pairwise difference is
large enough to tell regions apart on a 16 px tile, and every color stays bright
enough for the dark cat and cross sprites to read.

Motion effects stay cheap: the confirmed cat pops from 60% to 100%, the auto
crosses sweep outwards from that cat one ring at a time, a wrong guess shakes the
cell, and the win sequence scales all cats to 1.5x, rotates them, then fills the
progress bar one cat at a time. The board and the level grid are each drawn by
a single custom `LV_EVENT_DRAW_MAIN` callback, and the cat, cross, heart and star
are 8x8 pixel sprites, so the whole UI needs no image assets.

## Level ladder

| Levels | Boards | Cats | Intent |
| --- | --- | --- | --- |
| 1-3 | 4x4 | 3 | Fewest cats, largest regions |
| 4-6 | 4x4 | 4 | Full rows and columns |
| 7-8 | 5x5 | 4 | One empty row and column |
| 9-12 | 5x5 | 5 | Saturated 5x5 |
| 13-17 | 6x6 | 6 | Saturated 6x6 |
| 18-20 | 7x7 | 7 | Saturated 7x7 |
| 21-80 | 8x8 to 14x14 | 8-14 | Difficulty from region layout |

From level 21 on, every ten levels form one difficulty tier that mixes two
adjacent board sizes (8/9, 9/10, ... 13/14). The 14x14 cap is fixed; late tiers
are hard because region shapes compress the candidate space, not because the
board grows. Difficulty is measured by pure logical propagation (deduction
rounds, and how many candidates remain at the first branch) and is
non-decreasing inside each tier.

**Why the smallest board is 4x4:** with the three rules plus the uniqueness
requirement, 2x2 and 3x3 admit no valid level at all. On 2x2 a single cat can
sit in any of the four cells, so no partition can make the answer unique; on
3x3 every two cells are adjacent, so k=3 has no legal placement, k=1 is
uniquely unsolvable, and an exhaustive search over every answer set and every
two-region partition finds no unique k=2 level. 4x4 is the smallest board where
a uniquely solvable level exists.

### Endless levels

There is no last level. Levels 1-80 come from the Python generator, 81-200 from
the C generator in `tools/gen_catcube_extra.c` (same algorithm, deterministic
seeds, retried until each level has exactly one solution). Beyond 200 the level
is derived from the table by a fixed symmetry (transpose/mirror/rotate) plus a
color shift chosen from the level number. Symmetries are bijections, so the
uniqueness of every level is preserved, and the same level number always yields
the same board: resetting the game never changes level 100.

The full listing of levels 1-80 (Chinese, fixed format) is in
[`catcube-levels.txt`](catcube-levels.txt).

## Generation

`tools/gen_catcube_levels.py` generates the table by reverse construction:

1. place a cat set that already satisfies rules 1 and 3;
2. grow `k` four-connected regions from those cats, each region containing
   exactly one cat and covering the board;
3. enumerate every solution on the finished board; keep the level only when the
   solution count is exactly one. Otherwise kill the extra solutions by moving a
   non-answer cell of an extra solution into a region that already contains
   another cell of that solution (connectivity and "one cat per region" are
   re-checked after every move), then re-enumerate.

`python3 tools/gen_catcube_levels.py --check` re-validates the checked-in data
(region connectivity, one cat per region, rule compliance, unique solution,
C/JSON/TXT consistency, non-decreasing tier difficulty).

## Validation

```bash
./tools/validate.sh --static     # repository checks + host tests
./tools/validate.sh --firmware   # ESP-IDF build + merged image
```

`tests/test_catcube_logic.c` re-implements the solution counter in C and asserts
one solution for all 80 levels, independent of the Python generator, and covers
cursor wrap, cross/cat toggling, conflict masks and clear detection.

## Source layout

| File | Role |
| --- | --- |
| `main/app/catcube.c/.h` | NVS progress, input task with hold-to-repeat, lifecycle |
| `main/app/catcube_ui.c/.h` | Pages, self-drawn board, pixel sprites, key navigation |
| `main/app/catcube_logic.c/.h` | Marks, cursor, conflict masks, hint (host-testable) |
| `main/app/catcube_levels.h` | Level table contract |
| `main/app/catcube_levels.c` | Level provider: baked table + deterministic transform beyond it (200 levels) |
| `main/app/catcube_levels_data.c` | Generated level data, levels 1-80 |
| `main/app/catcube_levels_extra.c` | Generated level data, levels 81-200 |
| `main/app/catcube_audio.c` | Synthesized sound effects (no asset files) |
| `tools/gen_catcube_levels.py` | Generator and verifier for levels 1-80 |
| `tools/gen_catcube_extra.c` | Host tool that generates levels 81-200 |
| `tools/host/catcube_gen.c` | Deterministic level generator used by that tool |
| `tests/test_catcube_logic.c` | Host tests incl. uniqueness proof |
