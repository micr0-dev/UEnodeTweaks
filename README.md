# UEnodeTweaks
Quality-of-life tweaks for the Unreal Engine 5 Blueprint graph editor.

**Free and open source.** If it saves you time, consider [sponsoring on GitHub](https://github.com/sponsors/micr0-dev) or [buying a coffee on Ko-fi](https://ko-fi.com/micr0byte). It goes a long way.

> ⚠️ **Compatibility:** Built and tested against **Unreal Engine 5.4.4** only. It may work on other 5.x versions but has not been verified. Use on other versions at your own risk.

---

## Features

### Multi-Connect (Ctrl + Click)
Click an exec pin while holding Ctrl to connect it to multiple nodes. A Sequence node is automatically inserted to fan out the execution.

<img width="968" height="707" alt="image" src="https://github.com/user-attachments/assets/6863107a-10bb-4111-b2bd-faa03d6951f1" />

### Multi-Pin Drag (Shift + Click)
Shift-click to select multiple pins, then drag them onto another node. Pins are matched by type automatically.

<img width="1663" height="771" alt="image" src="https://github.com/user-attachments/assets/fbdc465e-f052-4efc-9ca4-30131bb078a1" />

### Smart Layout (Q)
Press Q with multiple nodes selected to auto-arrange them into clean columns. Nodes are sorted by dependency, spaced with padding, and centered on their original position.

<img width="1206" height="643" alt="image" src="https://github.com/user-attachments/assets/5105d2ee-09dd-4b13-8eb7-7d3b174a6c9b" />

### f(x) Math Expression Node
A pure Blueprint node that evaluates an inline math expression. Variables become input pins, result is a float output. Supports standard math functions (sin, cos, tan, asin, acos, atan, atan2, sqrt, pow, abs, min, max, clamp, lerp, floor, ceil, round, sign, exp, log, ln, mod). Type an expression directly into the node search to create one instantly. Editing the expression automatically marks the Blueprint for recompilation.

<img width="1418" height="654" alt="image" src="https://github.com/user-attachments/assets/e29d4226-4fda-4b87-883a-804f23f30a43" />

### Orthogonal Wire Routing
Replaces Bezier curves with 90-degree routed wires using A* pathfinding. Toggle in Editor Preferences → Plugins → UE Node Tweaks.

<img width="1840" height="1200" alt="2026-05-19-113132_hyprshot" src="https://github.com/user-attachments/assets/56e4e3e2-eade-48a0-be8f-4a262c70d81d" />

### Wire Bridges
Draws small hop-overs where wires cross, making dense graphs easier to read. Toggle in Editor Preferences → Plugins → UE Node Tweaks.

<img width="496" height="540" alt="image" src="https://github.com/user-attachments/assets/bf4d3656-a78a-435b-9a66-bc4316c6299a" />

### Hover Highlight (Ctrl + Hover)
Hold Ctrl and hover over a node to dim all unrelated nodes and wires. Only the hovered node and its direct neighbors stay fully visible. Fades in and out smoothly.

<img width="1276" height="595" alt="2026-05-19-085519_hyprshot" src="https://github.com/user-attachments/assets/56f1ce29-a2e3-4410-9844-a7ca2221a204" />

---

## Installation
Download the latest release, drop the `UEnodeTweaks` folder into your project's `Plugins/` directory, and restart the editor. Enable the plugin in **Edit → Plugins** if it isn't already. Settings are in **Editor Preferences → Plugins → UE Node Tweaks**.

---

## Support
If UEnodeTweaks is useful to you, you can support development here:

- [GitHub Sponsors](https://github.com/sponsors/micr0-dev)
- [Ko-fi](https://ko-fi.com/micr0byte)

Issues and PRs are welcome.

Published under `AGPLv3-or-later`
