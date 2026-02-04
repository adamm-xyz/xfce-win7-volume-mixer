# XFCE Volume Mixer Panel Plugin

A panel plugin for XFCE that provides per-application volume control, similar to the Windows volume mixer.

![Volume Mixer popup](screenshot.jpg)

## Features

- **Per-application volume control** — Adjust the volume of each application (e.g. Firefox, Spotify) independently
- **Application icons** — Each stream shows the application’s icon from PulseAudio metadata (with fallback)
- **Panel popup** — Click the panel icon to open a popup anchored to the panel; no floating window
- **Horizontal channel layout** — One column per application (icon, label, vertical slider)
- **Real-time updates** — Automatically updates when applications start or stop playing audio
- **PulseAudio / PipeWire** — Uses libpulse (works with PulseAudio and PipeWire-Pulse)

## Requirements

- GTK+ 3
- libxfce4panel-2.0
- libpulse, libpulse-mainloop-glib

### Debian / Ubuntu

```bash
sudo apt install build-essential cmake \
  libgtk-3-dev libxfce4panel-2.0-dev \
  libpulse-dev libxfce4util-dev
```

### Fedora

```bash
sudo dnf install gcc cmake gtk3-devel \
  xfce4-panel-devel pulseaudio-libs-devel \
  xfce4-dev-tools
```

### Arch Linux

```bash
sudo pacman -S base-devel cmake gtk3 xfce4-panel libpulse
```

## Building

**Recommended: CMake**

```bash
git clone https://github.com/adamm-xyz/xfce-volume-mixer-plugin.git
cd xfce-volume-mixer-plugin
mkdir build && cd build
cmake ..
make
sudo make install
```

**Alternative: Makefile**

```bash
make
sudo make install
```

## Installation

1. Restart the panel (if it was already running):
   ```bash
   xfce4-panel --restart
   ```
2. Right-click the panel → **Panel** → **Add New Items…**
3. Add **Volume Mixer**.

Click the volume icon on the panel to open the mixer popup.

## Usage

- **Click the panel icon** to open the volume mixer popup.
- Each application playing audio appears as a column with icon, name, and a vertical volume slider.
- Adjust sliders to change per-application volume.
- The popup closes when focus is lost or you click outside.

## Uninstallation

From the **build** directory (if you used CMake):

```bash
sudo make uninstall
```

Or remove files manually (paths may vary; use `pkg-config --variable=libdir libxfce4panel-2.0` to find the plugin dir):

```bash
sudo rm -f /usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libvolume-mixer-plugin.so
sudo rm -f /usr/share/xfce4/panel/plugins/volume-mixer-plugin.desktop
xfce4-panel --restart
```

## Troubleshooting

- **Plugin not in the list** — Restart the panel: `xfce4-panel --restart`. If it still doesn’t appear, log out and back in.
- **No applications in mixer** — Ensure PulseAudio (or PipeWire with Pulse compat) is running: `pulseaudio --check` or `pactl info`.
- **Build errors** — Install the development packages for GTK3, libxfce4panel, and libpulse as above.

## Technical details

- **Language:** C (C99)
- **UI:** GTK 3, libxfce4panel
- **Audio:** PulseAudio (libpulse) — works with PipeWire when using the Pulse compatibility layer.

## License

GPL v2 or later. See [LICENSE](LICENSE).

## Contributing

Contributions are welcome. Open an issue or pull request on GitHub.
