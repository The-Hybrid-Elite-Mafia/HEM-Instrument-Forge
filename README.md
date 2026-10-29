# HEM Instrument Forge

A standalone Windows app from The Hybrid Elite Mafia for making instrumentals without owning instruments.

- **BAND**: pick a style, key, tempo and length, choose which parts play and which instrument plays each one, then press **WRITE THE SONG**. Every part becomes its own track. **BALANCE THE MIX** sets the track levels by measuring each part.
- **TRACKS**: an overview of the whole song, plus a note editor for each track. You can draw, move, resize, set velocity, quantize, duplicate, and undo or redo any change.
- **STAGE**: a room seen from above with every player in it. Drag a player and the sound follows: level, tone, delay, early echoes and room all change with distance. There are seven rooms, plus a headphone 3D mode.
- **INSTRUMENTS**: every installed instrument. Play the selected track from the on-screen keyboard, your computer keys or a MIDI keyboard. You can also record what you play.
- **Export**: one WAV stem per track, a clean or mastered mix (Elite Master, -14 LUFS), and a MIDI file. Choose 16, 24 or 32-bit at 44.1, 48 or 96 kHz.

## The HEM Instrument Library

The library has two kinds of instruments:

- **Recorded instruments**, all public domain (CC0), built at fixed commits by `Tools/build_library.py`:
  - Versilian Community Sample Library
  - VS Chamber Orchestra 2: Community Edition
  - Virtuosity Drums
- **Modeled instruments**: original HEM synthesis code (`Source/Models`). `Tools/ModelRender` renders them into the same sample format, which covers guitars, basses, electric pianos, organ, accordion, choir, synths and the trap kit.

The rooms are simulated (image-source early echoes plus a feedback delay network), not recorded.

## Build

Building is done by the manual GitHub Actions workflow `.github/workflows/windows.yml`. It builds the app, runs the unit tests, builds the library, runs the end-to-end preview, makes the Inno Setup installer and publishes one Release.

To build locally:

```
cmake -S . -B build -DHF_BUILD_TESTS=ON -DHF_BUILD_APP=ON -DHF_BUILD_TOOLS=ON
cmake --build build --config Release
```

Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia.
