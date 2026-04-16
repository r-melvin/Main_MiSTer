# MiSTer Launcher Theme System

The launcher now supports customizable themes. Themes control all UI colors, font sizes, and optional background images.

## Quick Start

### Using a Built-in Theme

Add to `MiSTer.ini`:
```ini
LAUNCHER_THEME=dark
```

Available built-in themes:
- `dark` — Dark Blue (default) — warm gold highlights on cool blue
- `light` — Bright background with dark text
- `retro` — Green phosphor CRT aesthetic
- `purple` — Deep purple with magenta accents

### Creating a Custom Theme

1. Create a theme directory: `{launcher_path}/themes/{my_theme}/`
2. Create `theme.cfg` with color and font settings (see below)
3. Update `MiSTer.ini`: `LAUNCHER_THEME=my_theme`

## Theme File Format

Theme files are plain text with `KEY=VALUE` lines. Comments use `#`.

```ini
# Theme name (displayed in logs)
name = My Theme

# Colours (ARGB hex format)
# 6-char RRGGBB  → fully opaque (FF prepended)
# 8-char AARRGGBB → custom alpha
bg       = 0F0F19    # Background fill
card     = 323241    # Game/system cards
hi       = FFDC00    # Selection highlight
fav      = FFB400    # Favourite badge ring
text     = DCDCDC    # Primary text
dim      = 78788C    # Secondary/hint text
bar      = 1C1C2C    # Header/footer bars
overlay  = D2000000  # Modal overlay (alpha matters)
err      = DC3C3C    # Error text
search   = 28283C    # Search bar background

# Font sizes (points; valid: 8–72)
font_title = 28      # Large headings
font_big   = 22      # Medium text
font_sm    = 17      # Small text / labels

# Optional background image
# Absolute path on SD card; leave blank to use solid colour
# bg_image = /media/fat/launcher/themes/my_theme/bg.jpg
```

## Colour Reference

| Key | Usage | Default |
|-----|-------|---------|
| `bg` | Background fill (when no image) | `0F0F19` (dark blue) |
| `card` | Game card backgrounds | `323241` (dark gray) |
| `hi` | Selection highlight | `FFDC00` (gold) |
| `fav` | Favourite badge ring | `FFB400` (orange) |
| `text` | Primary text | `DCDCDC` (light gray) |
| `dim` | Secondary/hint text | `78788C` (medium gray) |
| `bar` | Header/footer bars | `1C1C2C` (very dark blue) |
| `overlay` | Semi-transparent modal overlay | `D2000000` (82% black) |
| `err` | Error text | `DC3C3C` (red) |
| `search` | Search bar background | `28283C` (dark) |

## Background Images

To use a custom background image:

1. Create a JPEG/PNG at your chosen path (e.g., `/media/fat/launcher/themes/my_theme/bg.jpg`)
2. Set the full absolute path in `theme.cfg`: `bg_image = /media/fat/launcher/themes/my_theme/bg.jpg`
3. Image will be scaled to fill the screen (1280×720 or your configured resolution)

If the image fails to load, the launcher falls back to the solid `bg` colour.

## Example: Dark Theme with Background

```ini
name = Dark with Background

bg       = 0F0F19
card     = 323241
hi       = FFDC00
fav      = FFB400
text     = DCDCDC
dim      = 78788C
bar      = 1C1C2C
overlay  = D2000000
err      = DC3C3C
search   = 28283C

font_title = 28
font_big   = 22
font_sm    = 17

bg_image = /media/fat/launcher/themes/dark/bg.jpg
```

## Layout

Bundled theme examples are included in the source:
```
themes/
├── dark/
│   └── theme.cfg          (Dark Blue — default)
├── light/
│   └── theme.cfg          (Light — bright background)
├── retro/
│   └── theme.cfg          (Retro — green phosphor)
└── purple/
    └── theme.cfg          (Purple — magenta accents)
```

Copy these to your SD card and modify as needed:
```
/media/fat/launcher/themes/{name}/theme.cfg
```

## INI Configuration

Add to `MiSTer.ini` (in `[LAUNCHER]` section if it exists):
```ini
LAUNCHER=1
LAUNCHER_PATH=/media/fat/launcher
LAUNCHER_THEME=dark
LAUNCHER_PARTICLES=1
```

- `LAUNCHER_THEME=default` or empty → use built-in dark blue theme
- `LAUNCHER_THEME=light` → load `{launcher_path}/themes/light/theme.cfg`
- If the theme file doesn't exist, falls back to built-in defaults

## Technical Notes

- Theme colours are runtime-loaded; no recompilation needed
- Font sizes are applied at startup (before drawing begins)
- Background image is scaled once at init; no performance penalty during rendering
- All colour constants in the launcher code (`LC_BG`, `LC_HI`, etc.) are backed by the theme struct
- Theme is loaded before the launcher draws anything, so splash screen uses theme colours

## Hex Color Examples

| Colour | Hex | Usage |
|--------|-----|-------|
| Pure black | `000000` | Text/overlays |
| Pure white | `FFFFFF` | Highlights |
| Dark blue | `0F0F19` | Backgrounds |
| Gold | `FFDC00` | Highlights |
| Green | `22CC22` | Retro text |
| Red | `FF0000` | Errors |
| Purple | `CC77FF` | Accents |

Prepend `FF` for opaque (e.g., `FF` + `FFDC00` = `FFFFDC00`). For transparency, use 8-char format with alpha (e.g., `D2000000` = 82% opacity black).
