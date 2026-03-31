---
name: skore-capture-screenshot
description: Launch SkullbonezCore, wait for it to render, capture a screenshot of the window, and display it for visual verification. Use when you need to verify rendering output looks correct.
---

## Capture Screenshot of SkullbonezCore

This skill launches the Debug exe, waits for it to render a few frames, then captures a screenshot of the window for visual inspection.

### Usage

Run the capture script, then view the resulting screenshot:

```pwsh
powershell -ExecutionPolicy Bypass -File "Y:\SkullbonezCore\Skills\capture-screenshot\capture-screenshot.ps1" -DelaySeconds 3 -OutputPath "Y:\SkullbonezCore\Debug\screenshot.png"
```

Adjust `-DelaySeconds` to control how long to wait before capturing (default: 3 seconds).

### After capture

View the screenshot image using the `view` tool:

```
view path="Y:\SkullbonezCore\Debug\screenshot.png"
```

The view tool will return the image as base64 so you can see exactly what the engine rendered (or any error dialogs that appeared).

### What to look for

- **Black window**: OpenGL context created but nothing rendered (shader/draw issue)
- **Error dialog**: An exception was thrown — read the message text
- **Process crashed**: Script will report exit code and capture full screen for any dialogs
- **Normal rendering**: Terrain, skybox, spheres, water, shadows, FPS text visible

### If the process exits immediately

The script detects this and captures a full-screen screenshot to catch any error MessageBox dialogs that may be visible.
