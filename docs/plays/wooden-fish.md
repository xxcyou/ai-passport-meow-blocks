<p align="right">
  <a href="wooden-fish.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Wooden Fish: a merit-counting play

Wooden Fish turns the AI Passport into a pocket merit counter. One press of the
OK key strikes the fish, plays a synthesized knock, and raises the count. The
counter survives power loss, the combo streak is visible, and the timbre can be
switched without a computer.

This is a derivative application built on the baseline BSP. It does not use the
baseline test menu, its screens, or `ui_pixel` helpers; see the
[mandatory UI redesign rule](../development/ai-guide.md#mandatory-ui-redesign-for-derivative-applications).

## Controls

| Page | Key | Short press | Long press (500 ms) |
| --- | --- | --- | --- |
| Strike | OK | strike once (count +1) | open the reset confirmation |
| Strike | UP | switch timbre: wood block, bell, drum | open the merit ledger |
| Strike | DOWN | toggle mute | - |
| Reset confirmation | OK | clear the count | clear the count |
| Reset confirmation | UP / DOWN | cancel | cancel |
| Ledger | OK | - | return to striking |

A single click that arrives within 400 ms after a long press is ignored: the
button component can emit both events for one physical press, and without this
guard "long press OK" would immediately confirm the reset it just opened.

## Pages

- **Strike page**: title, battery percentage (top right), the wooden fish drawn
  from three rounded shapes, the merit count in a 48 px numeric font, the unit
  label, a combo badge, and two hint lines. A strike scales the fish, expands a
  fading ring, floats a `+1` upward, and pops the count.
- **Ledger page**: session count, lifetime count, best combo, current timbre,
  and mute state.
- **Reset confirmation**: a dimmed overlay with an amber panel; only OK clears
  the count.

## Behaviour and data

| Item | Rule |
| --- | --- |
| Combo | Two strikes within 1500 ms count as a streak; the streak caps at 999 |
| Count cap | 99,999,999; further strikes do not wrap the counter |
| Persistence | Lifetime count, best combo, timbre, and mute state live in NVS namespace `wf_merit` |
| Write policy | Strikes mark the state dirty and flush at most every 5 s from the input task; a reset and a settings change flush immediately |
| Session count | Deliberately not persisted; it restarts at zero on every boot |

There is no real-time clock on this board, so the ledger shows "session" and
"lifetime" rather than "today". Adding a daily figure would require a time
source and a separate decision about its accuracy.

## Sound

The knock is synthesized at runtime (`main/app/knock_synth.c`): two exponentially
decaying sine modes plus a short noise transient, rendered at 16 kHz, 16-bit
mono. Each timbre is a parameter set, and the combo raises the pitch by 6 Hz per
step up to +36 Hz. No audio asset is embedded, so no audio license applies and
changing a timbre costs no flash.

Playback runs in its own task and is the only writer of the I2S stream; the
button callback only enqueues, and a full queue drops the request instead of
blocking input.

## Chinese text and fonts

The UI copy lives in `main/app/app_strings.h`. `tools/gen_app_fonts.py` extracts
the non-ASCII characters from that header and renders `assets/fonts/app_cjk_16.c`
and `app_cjk_24.c` from Droid Sans Fallback with `lv_font_conv` 1.5.2. ASCII and
digits come from LVGL's built-in Montserrat through an application-owned fallback
chain, because the source font has no Latin glyphs.

Two checks keep the coverage honest:

- `tests/test_app_font_coverage.py` decodes the generated cmap and fails if any
  UI code point is missing, with a negative probe so it cannot pass vacuously.
- The application logs `app_fonts_check_coverage()` at boot and reports missing
  code points as `U+XXXX` on the console.

## Build and flash

```bash
source <esp-idf-v5.5.3>/export.sh
./tools/validate.sh            # static checks + host tests + merged firmware
```

The gate writes `build/FoloToy-AI-Passport-full.bin`, the merged image to flash
at offset `0x0`. A merged flash may reset stored data; see the
[flashing and data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

## Known limits

- No deep sleep: the play stays awake while it is open, so idle power is not
  optimised and no wake-key handling is implemented.
- Timbre and mute are the only settings; there is no brightness or volume menu.
- The strike animation is frame-driven at 16 ms; its smoothness on the panel is
  a device check, not something the host gate can prove.
