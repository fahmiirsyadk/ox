# oxbar

A minimal, extensible status bar and widget toolkit for X11 written in C.

## Why oxbar?

- **Lightweight**: ~1000 lines of C, no dependencies beyond X11
- **Fast**: Double-buffered rendering, no flicker
- **Extensible**: Plugin architecture — add widgets as independent `.c` files
- **Simple**: Plain text config, no Lua/Python/JSON
- **OSD widgets**: Volume/brightness progress bars triggered by keybindings

## Features

- Built-in widgets: time, date, cpu, mem, disk, net, vol, bright, bat, load, uptime
- External command widgets (run any shell command)
- Click handlers (launch apps on widget click)
- Mouse wheel support (scroll_up/scroll_down commands)
- Per-widget colors (fg/bg overrides)
- Hot config reload (`kill -HUP $(pidof oxbar)`)
- OSD bars (transient overlays with vertical progress bars)
- Multiple bars (left, center, right) with adaptive width
- EWMH dock window type (reserves screen space)

## Installation

### Dependencies

```bash
# Arch
sudo pacman -S libx11 libxft freetype2 fontconfig

# Debian/Ubuntu
sudo apt install libx11-dev libxft-dev libfreetype-dev libfontconfig1-dev

# Fedora
sudo dnf install libX11-devel libXft-devel freetype-devel fontconfig-devel
```

### Build

```bash
make
sudo make install
```

### Uninstall

```bash
sudo make uninstall
```

## Usage

```bash
oxbar                           # uses ~/.config/oxbar/config
oxbar /path/to/config           # custom config path
oxbar --debug                   # write logs to /tmp/oxbar-debug.log
oxbar /path/to/config --debug   # custom config + debug
```

### Signals

| Signal | Action |
|--------|--------|
| `SIGHUP` | Reload config and rebuild bars |
| `SIGUSR1` | Show volume OSD bar |
| `SIGUSR2` | Show brightness OSD bar |
| `SIGINT` / `SIGTERM` | Quit |

```bash
kill -HUP $(pidof oxbar)    # reload config
kill -USR1 $(pidof oxbar)   # show volume OSD
kill -USR2 $(pidof oxbar)   # show brightness OSD
```

## Configuration

Config lives at `~/.config/oxbar/config`. Format is hierarchical blocks:

```
bar left {
    position top
    alignment left
    height 24
    font monospace:size=11
    fg #ffffff
    bg #000000
    sep_color #555555
    padding 8

    widget time { interval 1 }
    widget sep  {}
    widget cpu  { icon CPU interval 2 cmd "awk '/^cpu /{u=$2+$4;s=$2+$3+$4+$5;printf \"%d%%\",(1-u/s)*100}' /proc/stat" }
    widget sep  {}
    widget mem  { icon MEM interval 2 cmd "free -m | awk '/^Mem:/{printf \"%ldMB\",$3}'" }
}

bar center {
    position top
    alignment center
    height 24
    widget date { interval 60 cmd "date +'%a %b %d'" }
}

bar vol_osd {
    type osd
    position left
    orientation vertical
    height 200
    width 30
    timeout 3
    fg #ffffff
    bg #111111

    widget volume {
        icon VOL
        render progress
        interval 0.1
        cmd "pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+%' | head -1 | grep -oP '\\d+' || echo '0'"
        fg #4488ff
    }
}
```

### Bar options

| Key | Default | Description |
|-----|---------|-------------|
| `position` | `top` | `top` or `bottom` |
| `alignment` | `left` | `left`, `center`, `right`, or `stretch` |
| `type` | `normal` | `normal` or `osd` |
| `orientation` | `horizontal` | `horizontal` or `vertical` |
| `timeout` | `3` | OSD auto-hide seconds |
| `height` | `24` | Bar height in pixels |
| `width` | auto | Bar width (for vertical bars) |
| `font` | `monospace:size=11` | Xft font name |
| `fg` | `#ffffff` | Foreground color (hex) |
| `bg` | `#000000` | Background color (hex) |
| `sep_color` | `#555555` | Separator color (hex) |
| `padding` | `8` | Padding between widgets (px) |

### Widget options

| Key | Default | Description |
|-----|---------|-------------|
| `interval` | varies | Update interval in seconds |
| `icon` | none | Icon/label prefix |
| `cmd` | none | Shell command for widget output |
| `click` | none | Shell command to run on click |
| `scroll_up` | none | Shell command for mouse wheel up |
| `scroll_down` | none | Shell command for mouse wheel down |
| `render` | `text` | `text` or `progress` (for OSD bars) |
| `fg` | bar fg | Per-widget foreground color |
| `bg` | none | Per-widget background color |

### Plugins

Widgets are independent `.c` files in `plugins/`. To add a new widget:

1. Create `plugins/foo.c`:

```c
#include "oxbar.h"
#include "plugin.h"

static Widget *create(void) {
    return widget_create("foo", NULL, 2.0, update_cmd, NULL);
}
void plugin_init_foo(void) { plugin_register("foo", create); }
```

2. Add to `src/plugin.c`:

```c
extern void plugin_init_foo(void);
// in plugin_init_all():
plugin_init_foo();
```

3. Use in config:

```
widget foo { icon FOO interval 2 cmd "your-command" }
```

## TODO

- [ ] Gradient backgrounds
- [ ] Image/icon support (PNG via Imlib2)
- [ ] Tooltip popups
- [ ] Multi-monitor support (per-output bars)
- [ ] Workspace indicators
- [ ] Systray integration
- [ ] DBus interface for external control
- [ ] Man page

## Alternatives

- [polybar](https://github.com/polybar/polybar) - Feature-rich, complex config
- [lemonbar](https://github.com/LemonBoy/bar) - Minimal, no built-in widgets
- [xmobar](https://codeberg.org/xmobar/xmobar) - Haskell, powerful but heavy
- [i3status](https://github.com/i3/i3status) - i3-specific, limited customization

## Credits

Built with [Xft](https://www.freedesktop.org/wiki/Software/Xft/) for font rendering. Inspired by [suckless](https://suckless.org/) philosophy.
