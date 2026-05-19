# UEnodeTweaks

Quality-of-life tweaks for the Unreal Engine 5 Blueprint graph editor.

## Features

### Multi-Connect (Ctrl + Click)
Click an exec pin while holding Ctrl to connect it to multiple nodes. A Sequence node is automatically inserted to fan out the execution.

### Multi-Pin Drag (Shift + Click)
Shift-click to select multiple pins, then drag them onto another node. Pins are matched by type automatically.

### Smart Layout (Q)
Press Q with multiple nodes selected to auto-arrange them into clean columns. Nodes are sorted by dependency, spaced with padding, and centered on their original position. No overlaps.

### f(x) Math Expression Node
A pure Blueprint node that evaluates an inline math expression. Variables become input pins, result is a float output. Supports standard math functions (sin, cos, tan, asin, acos, atan, atan2, sqrt, pow, abs, min, max, clamp, lerp, floor, ceil, round, sign, exp, log, ln, mod). Type an expression directly into the node search to create one instantly. Editing the expression automatically marks the Blueprint for recompilation.

### Orthogonal Wire Routing (EXPERIMENTAL)
Replaces Bezier curves with 90-degree routed wires using A* pathfinding. Toggle in Project Settings > Node Tweaks.

### Wire Bridges
Draws small hop-overs where wires cross, making dense graphs easier to read. Toggle in Project Settings > Node Tweaks.

### Hover Highlight (Ctrl + Hover)
Hold Ctrl and hover over a node to dim all unrelated nodes and wires. Only the hovered node and its direct neighbors stay fully visible. Fades in and out smoothly.
