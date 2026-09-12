# Contributing

Notes for anyone (human or AI) sending a PR to this repo. See
[README.md](README.md) for what the project is, how to build it, and how
it's laid out.

## Coding style

- **C only, no C++**, anywhere in the firmware (see README "Building").
- **Name things for what they hold, not just their type.** A loop counter
  like `int i` doesn't say what's being counted - once there's more than one
  loop in a file, or the array it indexes means something specific, that
  matters. Prefer a name tied to what's being iterated:
  `for (size_t entry_i = 0; entry_i < count; entry_i++)`,
  `for (ui_page_t page = 0; page < UI_PAGE_COUNT; page++)`. This isn't a
  blanket ban on short names - `x`/`y` for pixel coordinates, `c` for a
  character, `f` for a `FILE *`, or a callback's own conventional parameter
  (LVGL's `lv_timer_t *t`, `lv_event_t *e`) are already unambiguous and
  renaming them adds noise instead of removing it. The distinction is
  "generic counter with no other name to give it" vs. "idiomatic name for a
  well-known role."
- **Index a fixed-size array by name, not by raw number, once each slot
  means something different.** If you find yourself writing
  `s_screens[0]`, `s_screens[1]`, `s_screens[2]`... where each index is a
  distinct, specific thing (a particular screen, a particular version
  component), add an enum and index through that instead
  (`ui_page_t` in [ui.c](components/ui/ui.c),
  `version_part_t` in [app_ota.c](components/app_ota/app_ota.c)) - a
  reader shouldn't have to remember that slot 2 is the album screen.
  This does **not** apply to plain iteration over a homogeneous array
  (looping over Wi-Fi scan results, an httpd route table, pixels in an
  image) - there every slot means the same kind of thing, so a numeric
  index is already the whole story.
- **Comment the "why," not the "what."** The code already says what a line
  does; a comment earns its place by explaining a reason, trade-off, or
  gotcha a future reader (including you, in six months) would otherwise
  have to re-derive - see the bring-up log in
  [bsp_display.c](components/bsp/bsp_display.c) or the gesture/scroll
  interaction note in [ui.c](components/ui/ui.c) for the level of detail
  this codebase expects for anything non-obvious.
- **Match the file you're editing before introducing a new pattern.** If
  every other screen module in `components/ui/` structures its
  create/update/on_show functions a certain way, a new screen should too,
  even if you'd personally organize it differently from scratch.

## Project conventions

- **One ESP-IDF component per concern.** `app_config`, `app_wifi`,
  `app_photo`, `ui`, etc. - see README's "Project layout". A new
  self-contained feature usually wants its own component rather than
  growing an existing one into a second responsibility.
- **Config fields live in `app_config.h`/`app_config.c`, one NVS key
  per field** - not a serialized blob. Adding a setting means adding its
  field, its `KEY_*` string, and a `load_*`/`nvs_set_*` call; see the doc
  comment at the top of `app_config.h` for why (a device that hasn't seen a
  new key yet just gets its default, nothing else stored is disturbed).
- **Keep README.md and README.ko.md in sync.** Both are maintained
  side by side - a user-facing change (a new screen, a new web UI setting,
  a new config option) gets a line in both, not just one.

## Workflow

- Don't commit straight to `main` - branch, then open a PR. That's how
  every change so far has landed (see the merge commits in `git log`).
- [`.github/workflows/build.yml`](.github/workflows/build.yml) builds the
  firmware for `esp32s3` on every PR - it needs to pass before merging.
- No test suite exists yet beyond that build check; changes affecting
  hardware-facing behavior (display, touch, audio, Wi-Fi) are otherwise
  verified by flashing and checking on real hardware.
