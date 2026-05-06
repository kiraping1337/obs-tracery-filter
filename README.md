<div align="center">
   <img width="480" height="270" alt="tracery (2)" src="https://github.com/user-attachments/assets/6b12ec6f-9a6a-497b-b5f4-6160626a2b23" />
</div>

[Читать на русском](README.ru.md)

# Tracery Filter — OBS Plugin

A video filter plugin for OBS Studio that detects areas of a specific color in real time, draws bounding boxes around them, and connects them with animated lines. Inspired by the Tracery plugin for Adobe After Effects.

## Features

- **Color detection** — detects regions of a specified color using an adjustable threshold
- **Bounding boxes** — draws rectangles around detected regions with customizable color and thickness
- **Corner style** — option to draw only the corners of the bounding box instead of full sides
- **Coordinate labels** — displays X/Y coordinates above each detected region with custom font, color and outline
- **Connection lines** — connects detected regions with smooth bezier curves; supports dashed lines and adjustable thickness and curvature
- **Center markers** — optional dot markers at the center of each region
- **Two detection modes** — standard per-pixel detection and grid-based detection for more stable results on noisy sources (e.g. webcams)
- **Smoothing** — temporal smoothing of bounding box positions between frames
- **Performance controls** — adjustable scan step and frame skip to balance quality vs. CPU usage
- Works on any OBS source: game capture, screen capture, webcam, video capture devices, and scenes

## Requirements

- OBS Studio 30.0 or newer
- Windows (the plugin uses Windows GDI for text rendering)

## Installation

1. Download the latest release from the [Releases](../../releases) page
2. Copy `tracery-plugin-for-obs.dll` to:
   ```
   C:\Program Files\obs-studio\obs-plugins\64bit\
   ```
3. Copy the `data` folder contents to:
   ```
   C:\Program Files\obs-studio\data\obs-plugins\tracery-plugin-for-obs\
   ```
4. Restart OBS Studio

## Usage

1. Right-click any source in OBS → **Filters**
2. Click **+** → select **Tracery Filter**
3. Pick a **Key Color** to detect
4. Adjust **Threshold** until the desired regions are highlighted
5. Tune the remaining settings to your liking

## Settings

### Detection
| Setting | Description |
|---|---|
| Key Color | The color to search for |
| Threshold | How closely a pixel must match the key color (0 = exact) |
| Smoothing | Temporal smoothing of blob positions between frames |
| Min Distance | Minimum distance between separate blobs |
| Min Blob Size | Minimum blob size in pixels; smaller blobs are ignored |
| Detection Quality | Scan step size — higher values are faster but less precise |
| Update Every N Frames | How often detection runs; higher values reduce CPU load |

### Alternative Detection
Grid-based detection mode — recommended for webcams and noisy sources.

| Setting | Description |
|---|---|
| Alternative Detection | Enable grid-based detection |
| Grid Cell Size | Size of each grid cell in pixels |
| Cell Threshold | Minimum number of matching pixels required to activate a cell |

### Bounding Boxes
| Setting | Description |
|---|---|
| Show Boxes | Toggle bounding box visibility |
| Box Color | Color of the bounding box |
| Boxes Thickness | Line thickness of the bounding box |
| Corner Style | Draw only corners instead of full sides |
| Corner Length | Length of each corner segment |

### Labels
| Setting | Description |
|---|---|
| Show Labels | Toggle coordinate label visibility |
| Font | Font family and size |
| Text Color | Label text color |
| Outline | Enable text outline |
| Outline Color | Outline color |
| Outline Thickness | Outline thickness in pixels |

### Markers
| Setting | Description |
|---|---|
| Show Center Markers | Toggle center dot markers |
| Marker Color | Color of the markers |

### Connection Lines
| Setting | Description |
|---|---|
| Show Lines | Toggle connection lines |
| Line Color | Color of the lines |
| Line Thickness | Thickness of the lines |
| Curvature | How curved the bezier lines are (0 = straight) |
| Dashed Lines | Enable dashed line style |
| Dash Length | Length of each dash |
| Gap Length | Length of each gap between dashes |

## Building from Source

### Requirements
- Visual Studio 2022
- CMake 3.30+

### Steps

```cmd
git clone https://github.com/kiraping1337/obs-tracery-filter.git
cd obs-tracery-filter
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo
```

## License

This plugin is licensed under the GNU General Public License v2. See [LICENSE](LICENSE) for details.
