# Branding

USB Restoration Tool uses a USB drive inside an open restore loop. The name
stays descriptive; the tagline is **A fresh start for your USB drive.**

## Assets

- [app.svg](../resources/icons/app.svg) is the master application icon. Keep the
  transparent outside corners, ink tile, white drive and mint loop together.
- `app.png` is the 256 px Linux desktop icon. The app uses the scalable SVG.
- `app.ico` contains 16, 20, 24, 32, 40, 48, 64, 128 and 256 px Windows icons.
- [readme-banner.svg](../resources/branding/readme-banner.svg) is the editable
  project banner; the README uses its PNG export for consistent text rendering.
- `docs/images/app-light.png` and `app-dark.png` show the actual widgets with an
  example drive, rather than a physical device.

Use the same icon for the executable, taskbar, Linux launcher, header and About
dialog. Do not stretch it or put text inside the tile.

## Color and typography

| Role | Light | Dark |
| --- | --- | --- |
| Background | `#f3f6f5` | `#101c1f` |
| Surface | `#ffffff` | `#18282b` |
| Text | `#183235` | `#edf6f2` |
| Secondary text | `#586e6b` | `#a0b8b2` |
| Accent | `#087f70` | `#79e2bf` |
| Selected drive | `#def3eb` | `#24483f` |
| Destructive action | `#b91c1c` | `#c9343e` |

The interface uses Segoe UI, with Noto Sans and the system sans-serif as
fallbacks. Titles use semibold or bold type; supporting text stays quieter.
Thin borders and generous spacing separate surfaces without drop shadows.

Mint identifies selection, progress and ordinary actions. Erase actions stay
red; warnings use a warm surface and explicit text. Status messages retain
words and symbols so color is never the only cue. Preserve the acknowledgement
dialog and disk identity checks when adjusting the interface.

## Export

The optional export tool uses the existing Qt kit and its SVG image plugin.
It is excluded from the default build and from shipped packages. From the
repository root on Windows:

```powershell
cmake --build --preset windows --target usb-branding-export
$env:PATH = "C:/path/to/Qt/msvc2022_64/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "C:/path/to/Qt/msvc2022_64/plugins"
./build/Release/usb-branding-export.exe .
```

On Linux:

```bash
cmake --build --preset linux --target usb-branding-export
./build-linux/usb-branding-export . -platform offscreen
```

This exports PNG artwork and all nine ICO sizes from the SVG sources. Commit
the exports alongside source changes; packaging uses them without rendering
tools. Review the icon at 16, 32 and 256 px and the interface in both themes.

For headless Windows export, add `-platform offscreen` and set
`QT_QPA_FONTDIR` to `C:/Windows/Fonts` so banner text can find its fonts.
