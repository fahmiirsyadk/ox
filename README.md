# oxbar

A minimal, extensible status bar for X11 written in C.

## Why oxbar?

- **Lightweight**: ~1000 lines of C, no dependencies beyond X11
- **Fast**: Double-buffered rendering, no flicker
- **Extensible**: Add custom widgets with shell commands or C callbacks
- **Simple**: Plain text config, no Lua/Python/JSON
- **Unix philosophy**: Do one thing well

## Features

- Built-in widgets: time, date, cpu, mem, disk, net, vol, bright, bat, load, uptime
- External command widgets (run any shell command)
- Click handlers (launch apps on widget click)
- Per-widget update intervals
- Configurable colors, fonts, padding
- Multiple bars (top/bottom)
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

Binary installs to `/usr/bin/oxbar`, default config to `~/.config/oxbar/config`.

### Uninstall

```bash
sudo make uninstall
```

## Usage

```bash
oxbar                    # uses ~/.config/oxbar/config
oxbar /path/to/config    # custom config path
OXBAR_CONFIG=/path/to/config oxbar  # env var override
```

## Configuration

Config lives at `~/.config/oxbar/config`. Format is hierarchical blocks:

```
bar main {
    position top
    height 24
    font monospace:size=11
    fg #ffffff
    bg #000000
    sep_color #555555
    padding 8

    widget time {
        interval 1
    }

    widget sep {}

    widget cpu {
        icon CPU
        interval 1
    }

    widget mem {
        icon MEM
        interval 1
    }

    widget sep {}

    widget vol {
        icon VOL
        interval 1
        click cmd "pavucontrol"
    }

    widget date {
        interval 60
    }
}
```

### Bar options

| Key | Default | Description |
|-----|---------|-------------|
| `position` | `top` | `top` or `bottom` |
| `height` | `24` | Bar height in pixels |
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
| `click cmd` | none | Shell command to run on click |
| `cmd` | none | Shell command for external widget |

### Built-in widgets

| Widget | Default interval | Description |
|--------|------------------|-------------|
| `time` | 1s | Current time (HH:MM:SS) |
| `date` | 60s | Current date (Day Mon DD) |
| `cpu` | 1s | CPU usage percentage |
| `mem` | 1s | Memory usage in MB |
| `disk` | 30s | Disk usage (used/total) |
| `net` | 1s | Network RX/TX in MB |
| `vol` | 1s | PulseAudio volume |
| `bright` | 5s | Backlight brightness % |
| `bat` | 30s | Battery percentage |
| `load` | 1s | System load average |
| `uptime` | 60s | System uptime |
| `sep` | - | Separator (`|`) |

### External command widgets

Run any shell command and display its output:

```
widget weather {
    icon WEATHER
    cmd "curl -s wttr.in/?format=3 | cut -d: -f2"
    interval 300
}
```

The command's stdout becomes the widget label. Exit code is ignored.

## Extending with custom widgets

### Shell command widgets

For simple data sources, use external command widgets (see above). No recompilation needed.

### C callback widgets

For performance-critical or complex widgets, add a C implementation:

**1. Add update function in `widget.c`:**

```c
static void update_mywidget(void *ctx, char *buf, size_t len) {
    (void)ctx;
    // Read data from /proc, /sys, or compute it
    snprintf(buf, len, "value: %d", some_value);
}
```

**2. Add create function in `widget.c`:**

```c
Widget *widget_mywidget_create(void) {
    return widget_create("mywidget", NULL, 1.0, update_mywidget, NULL, NULL);
}
```

Parameters: `name`, `icon`, `interval`, `update_fn`, `click_fn`, `ctx`.

**3. Declare in `widget.h`:**

```c
Widget *widget_mywidget_create(void);
```

**4. Register in `bar.c` `bar_create()` widget factory:**

```c
} else if (strcmp(name, "mywidget") == 0) {
    widget = widget_mywidget_create();
}
```

**5. Use in config:**

```
widget mywidget {
    icon MW
    interval 1
}
```

### Widget callback signature

```c
typedef void (*WidgetUpdate)(void *ctx, char *buf, size_t len);
typedef void (*WidgetClick)(void *ctx);
```

- `ctx`: User data passed at creation (e.g., command string for external widgets)
- `buf`: Buffer to write label into (256 bytes)
- `len`: Buffer size
- Click handlers receive `ctx` and can fork/exec commands

### Example: Temperature widget

```c
static void update_temp(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f == NULL) {
        snprintf(buf, len, "??°C");
        return;
    }
    int temp = 0;
    fscanf(f, "%d", &temp);
    fclose(f);
    snprintf(buf, len, "%d°C", temp / 1000);
}

Widget *widget_temp_create(void) {
    return widget_create("temp", NULL, 5.0, update_temp, NULL, NULL);
}
```

## Architecture

```
main.c          Entry point, main loop, event handling
bar.c           Window creation, double-buffered rendering
widget.c        Widget implementations and lifecycle
config.c        Hierarchical config parser
```

### Rendering pipeline

1. Main loop updates widgets based on intervals
2. `bar_render()` clears back buffer (Pixmap)
3. Draws widgets to back buffer via Xft
4. Copies back buffer to window (atomic update, no flicker)
5. Flushes X11 commands

### Event loop

- 100ms tick (10 FPS)
- Processes ButtonPress events
- Calls widget click handlers

## Contributing

1. Fork the repo
2. Create a feature branch
3. Make changes
4. Test with `make clean && make`
5. Submit PR

### Code style

- C11 standard
- 4-space indent
- K&R braces
- No trailing whitespace
- Meaningful variable names
- Comments for non-obvious logic

## License

MIT License - see LICENSE file for details.

## TODO

- [ ] Hot config reload (SIGHUP)
- [ ] Per-widget colors
- [ ] Gradient backgrounds
- [ ] Image/icon support (PNG via Imlib2)
- [ ] Mouse wheel events
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

oxbar sits between lemonbar (too minimal) and polybar (too complex).

## Credits

Built with [Xft](https://www.freedesktop.org/wiki/Software/Xft/) for font rendering.

Inspired by [dwm](https://dwm.suckless.org/) and [suckless](https://suckless.org/) philosophy.
