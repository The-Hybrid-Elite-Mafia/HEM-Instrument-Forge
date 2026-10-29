#!/usr/bin/env python3
"""
HEM Instrument Library builder.

Turns free, public-domain (CC0) recordings into the instrument files HEM Instrument Forge plays:

  Versilian Community Sample Library (VCSL), CC0      github.com/sgossner/VCSL  (branch "sfz")
      keys, harps, zithers, recorders, ocarinas, saxophones, harmonicas, mallets, bells, world
      instruments and ~50 percussion instruments. Its own SFZ maps give the key / velocity /
      round-robin layout of every sample.
  VS Chamber Orchestra 2: Community Edition, CC0      github.com/sgossner/VSCO-2-CE
      solo and section strings, brass, woodwinds (mapped from the file names).
  Virtuosity Drums, CC0                               github.com/sfzinstruments/virtuosity_drums
      jazz drum kit (General MIDI notes).

Every instrument becomes a folder with instrument.json + 48 kHz 24-bit FLAC samples. The app
streams long samples from disk, so the library can be large.

What this script measures instead of trusting file names:
  * the octave of every VSCO file (YIN pitch detection; libraries disagree on octave numbering)
  * tuning offsets (stored per sample where the instrument is meant to be in tune)
  * each instrument's playing level, so every instrument sits at about the same loudness
Sustained articulations get a crossfade loop so held notes don't run out.

Usage:
  build_library.py --vcsl DIR --vsco DIR --vd DIR --out DIR [--models DIR] [--only id,id] [--jobs N]
  build_library.py --index DIR          (only rewrite index.json from the instrument folders)

--models: folder written by the ModelRender tool (modeled instruments, already in library format);
they are copied in and listed in the index.
"""
import argparse, json, math, os, re, shutil, sys, hashlib, traceback
from multiprocessing import Pool

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

OUT_RATE = 48000
LIB_VERSION = 1
NOTE = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6,
        'G': 7, 'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}
DYN = ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff']

# Default stage seats per family: x -1 (left) .. 1 (right), depth 0 (front) .. 1 (back), height 0..1
SEATS = {
    'Keys': (-0.45, 0.30, 0.25), 'Strings': (-0.55, 0.35, 0.20), 'Brass': (0.45, 0.75, 0.30),
    'Woodwinds': (0.05, 0.55, 0.25), 'Guitars & Plucked': (0.40, 0.25, 0.20), 'Bass': (0.15, 0.40, 0.15),
    'Folk & Free Reeds': (-0.20, 0.20, 0.25), 'Mallets & Bells': (0.30, 0.65, 0.30),
    'Percussion': (0.25, 0.80, 0.25), 'Drum Kits': (0.0, 0.70, 0.20), 'World': (-0.30, 0.45, 0.20),
    'Synths': (0.0, 0.25, 0.30), 'Choir & Pads': (0.0, 0.85, 0.45), 'Sound Effects': (0.0, 0.50, 0.30),
}

CREDITS = {
    'vcsl': ("Versilian Community Sample Library (VCSL) - Versilian Studios LLC and contributors. "
             "CC0 1.0 (public domain). https://github.com/sgossner/VCSL"),
    'vsco': ("VS Chamber Orchestra 2: Community Edition - recorded by Sam Gossner & Simon Dalzell, sample "
             "cutting by Elan Hickler/Soundemote, Versilian Studios LLC. CC0 1.0. https://github.com/sgossner/VSCO-2-CE"),
    'vd':   ("Virtuosity Drums - performed by Austin McMahon at Virtuosity Musical Instruments, Boston. CC0 1.0. "
             "https://github.com/sfzinstruments/virtuosity_drums"),
    'hem':  "Modeled by HEM Instrument Forge (original code, no recordings).",
}

# ======================================================================================
# Catalog. Names are plain instrument descriptions (no maker or product names).
#   arts: (articulation name, source spec)
#     vcsl spec: path of the .sfz file inside VCSL (relative)
#     vsco spec: folder inside VSCO-2-CE
#   kind: pitched | perc (unpitched; plays on its own keys, no transposing)
#   sustain: articulations whose notes hold (they get a loop); mono: plays one note at a time
#   tune: store the measured tuning correction per sample
# ======================================================================================
def V(id, name, family, arts, kind='pitched', max_s=8.0, sustain=(), mono=False, tune=False,
      release=None, every_key=False, vel_track=0.6):
    return dict(id=id, name=name, family=family, src='vcsl', arts=arts, kind=kind, max_s=max_s,
                sustain=set(sustain), mono=mono, tune=tune, release=release, every_key=every_key,
                vel_track=vel_track)


def S(id, name, family, arts, max_s=8.0, sustain=(), mono=True, release=None, vel_track=0.55, octave_check=True):
    return dict(id=id, name=name, family=family, src='vsco', arts=arts, kind='pitched', max_s=max_s,
                sustain=set(sustain), mono=mono, tune=True, release=release, every_key=False,
                vel_track=vel_track, octave_check=octave_check)


A, C, E, I, M = 'Aerophones/', 'Chordophones/', 'Electrophones/', 'Idiophones/', 'Membranophones/'
EB, FA, LA, RA = A + 'Edge-blown Aerophones/', A + 'Free Aerophones/', A + 'Lip Aerophones/', A + 'Reed Aerophones/'
CC, ZI = C + 'Composite Chordophones/', C + 'Zithers/'
FI, PI, SI = I + 'Friction Idiophones/', I + 'Plucked Idiophones/', I + 'Struck Idiophones/'
OM, SM = M + 'Other Membranophones/', M + 'Struck Membranophones/'

CATALOG = [
    # ---------------- Keys
    V('grand-piano-a', 'Concert Grand Piano A', 'Keys', [('Sustain', ZI + 'Grand Piano, Kawai.sfz')], max_s=12.0, release=0.45),
    V('grand-piano-b', 'Concert Grand Piano B', 'Keys', [('Sustain', ZI + 'Grand Piano, Steinway B.sfz')], max_s=12.0, release=0.45),
    V('upright-piano-a', 'Upright Piano A', 'Keys', [('Sustain', ZI + 'Upright Piano, Knight.sfz')], max_s=10.0, release=0.35),
    V('upright-piano-b', 'Upright Piano B', 'Keys', [('Sustain', ZI + 'Upright Piano, Yamaha.sfz')], max_s=10.0, release=0.35),
    V('harpsichord-english', 'Harpsichord (English)', 'Keys',
      [('Normal', ZI + 'Harpsichord, English - Normal.sfz'), ('Lute Stop', ZI + 'Harpsichord, English - Lute.sfz')], max_s=6.0, release=0.12),
    V('harpsichord-flemish', 'Harpsichord (Flemish)', 'Keys',
      [("8 Foot", ZI + "Harpsichord, Flemish - 8'.sfz"), ("4 Foot", ZI + "Harpsichord, Flemish - 4'.sfz"),
       ('Full', ZI + 'Harpsichord, Flemish - Full.sfz')], max_s=6.0, release=0.12),
    V('harpsichord-french', 'Harpsichord (French)', 'Keys', [('Normal', ZI + 'Harpsichord, French.sfz')], max_s=6.0, release=0.12),
    V('harpsichord-italian', 'Harpsichord (Italian)', 'Keys', [('Normal', ZI + 'Harpsichord, Italian.sfz')], max_s=6.0, release=0.12),
    V('pipe-organ', 'Pipe Organ', 'Keys',
      [('Quiet', EB + 'Pipe Organ - Quiet.sfz'), ('Loud', EB + 'Pipe Organ - Loud.sfz'),
       ('Quiet Pedal', EB + 'Pipe Organ - Quiet Pedal.sfz'), ('Loud Pedal', EB + 'Pipe Organ - Loud Pedal.sfz')],
      max_s=8.0, sustain=('Quiet', 'Loud', 'Quiet Pedal', 'Loud Pedal'), release=0.5),
    V('chamber-organ', 'Chamber Organ', 'Keys',
      [("8 Foot", EB + "Renaissance Organ - 8'.sfz"), ("4 Foot", EB + "Renaissance Organ - 4'.sfz"),
       ("4 + 8 Foot", EB + "Renaissance Organ - 4'+8'.sfz"), ('Full', EB + 'Renaissance Organ - Full.sfz')],
      max_s=8.0, sustain=("8 Foot", "4 Foot", "4 + 8 Foot", 'Full'), release=0.25),
    V('fm-keys', 'FM Keys', 'Keys',
      [('Electric Piano', E + 'TX81Z - FM Piano.sfz'), ('Piano', E + 'TX81Z - Piano 1.sfz'),
       ('Clavi', E + 'TX81Z - Clavisynth.sfz')], max_s=6.0, release=0.3),
    # ---------------- plucked / zithers
    V('concert-harp', 'Concert Harp', 'Guitars & Plucked', [('Pluck', CC + 'Concert Harp.sfz')], max_s=8.0, release=1.5),
    V('folk-harp', 'Folk Harp', 'Guitars & Plucked', [('Pluck', CC + 'Folk Harp.sfz')], max_s=7.0, release=1.2),
    V('strumstick', 'Strumstick', 'Guitars & Plucked', [('Pluck', CC + 'Strumstick.sfz')], max_s=6.0, release=0.4),
    V('dan-tranh', 'Zither (Dan Tranh)', 'World',
      [('Normal', ZI + 'Dan Tranh - Normal.sfz'), ('Vibrato', ZI + 'Dan Tranh - Vibrato.sfz'),
       ('Tremolo', ZI + 'Dan Tranh - Tremolo.sfz'), ('Gliss', ZI + 'Dan Tranh - Gliss.sfz'), ('Effects', ZI + 'Dan Tranh - FX.sfz')],
      max_s=7.0, release=0.6),
    V('psaltery', 'Psaltery', 'World',
      [('Pluck', ZI + 'Psaltery, Bowed and Plucked - Pluck.sfz'), ('Long Bow', ZI + 'Psaltery, Bowed and Plucked - LongBow.sfz'),
       ('Spiccato', ZI + 'Psaltery, Bowed and Plucked - Spiccato.sfz')], max_s=7.0, sustain=('Long Bow',), release=0.5),
    # ---------------- winds (VCSL)
    V('recorder-soprano', 'Soprano Recorder', 'Woodwinds',
      [('Sustain', EB + 'Baroque Soprano Recorder - Sustain.sfz'), ('Staccato', EB + 'Baroque Soprano Recorder - Staccato.sfz')],
      sustain=('Sustain',), mono=True, release=0.12),
    V('recorder-alto', 'Alto Recorder', 'Woodwinds',
      [('Sustain', EB + 'Baroque Alto Recorder - Sustain.sfz'), ('Vibrato', EB + 'Baroque Alto Recorder - SusVib.sfz'),
       ('Staccato', EB + 'Baroque Alto Recorder - Staccato.sfz')], sustain=('Sustain', 'Vibrato'), mono=True, release=0.12),
    V('recorder-tenor', 'Tenor Recorder', 'Woodwinds',
      [('Sustain', EB + 'Baroque Tenor Recorder - Sustain.sfz'), ('Vibrato', EB + 'Baroque Tenor Recorder - SusVib.sfz'),
       ('Staccato', EB + 'Baroque Tenor Recorder - Staccato.sfz')], sustain=('Sustain', 'Vibrato'), mono=True, release=0.12),
    V('recorder-bass', 'Bass Recorder', 'Woodwinds',
      [('Sustain', EB + 'Baroque Bass Recorder - Sustain.sfz'), ('Vibrato', EB + 'Baroque Bass Recorder - SusVib.sfz'),
       ('Staccato', EB + 'Baroque Bass Recorder - Staccato.sfz')], sustain=('Sustain', 'Vibrato'), mono=True, release=0.12),
    V('ocarina-small', 'Small Ocarina', 'World',
      [('Sustain', EB + 'Ocarina, Small - Sustain.sfz'), ('Staccato', EB + 'Ocarina, Small - Staccato.sfz')],
      sustain=('Sustain',), mono=True, release=0.12),
    V('ocarina', 'Ocarina', 'World',
      [('Sustain', EB + 'Ocarina, Typical - Sus.sfz'), ('Vibrato', EB + 'Ocarina, Typical - SusVib.sfz')],
      sustain=('Sustain', 'Vibrato'), mono=True, release=0.12),
    V('tenor-sax', 'Tenor Saxophone', 'Woodwinds',
      [('Straight', RA + 'Tenor Saxophone - Non-Vibrato.sfz'), ('Vibrato', RA + 'Tenor Saxophone - Vibrato.sfz'),
       ('Staccato', RA + 'Tenor Saxophone - Staccato.sfz')], sustain=('Straight', 'Vibrato'), mono=True, release=0.15),
    V('saxello', 'Saxello (Curved Soprano Sax)', 'Woodwinds',
      [('Straight', RA + 'Saxello - Non-Vibrato.sfz'), ('Vibrato', RA + 'Saxello - Vibrato.sfz'),
       ('Staccato', RA + 'Saxello - Staccato.sfz')], sustain=('Straight', 'Vibrato'), mono=True, release=0.15),
    V('didgeridoo', 'Didgeridoo', 'World', [('Drone', LA + 'Didgeridoo.sfz')], max_s=10.0, mono=True, release=0.3),
    V('harmonica-c', 'Harmonica in C', 'Folk & Free Reeds',
      [('Normal', FA + 'Harmonica-Hohner-Special20-C - Normal.sfz'), ('Soft', FA + 'Harmonica-Hohner-Special20-C - Soft.sfz'),
       ('Vibrato', FA + 'Harmonica-Hohner-Special20-C - Vib.sfz')], sustain=('Normal', 'Soft', 'Vibrato'), mono=False, release=0.12),
    V('harmonica-f', 'Harmonica in F', 'Folk & Free Reeds',
      [('Normal', FA + 'Harmonica-Hohner-Special20-F - Normal.sfz'), ('Accented', FA + 'Harmonica-Hohner-Special20-F - Accented.sfz'),
       ('Hand Vibrato', FA + 'Harmonica-Hohner-Special20-F - HandVib.sfz'), ('Vibrato', FA + 'Harmonica-Hohner-Special20-F - Vib.sfz'),
       ('Staccato', FA + 'Harmonica-Hohner-Special20-F - Stac.sfz')],
      sustain=('Normal', 'Hand Vibrato', 'Vibrato'), release=0.12),
    V('harmonica-chromatic', 'Chromatic Harmonica', 'Folk & Free Reeds',
      [('Normal', FA + 'Harmonica-Hohner-Super64 - Normal.sfz'), ('Accented', FA + 'Harmonica-Hohner-Super64 - Accented.sfz'),
       ('Vibrato', FA + 'Harmonica-Hohner-Super64 - Vib.sfz')], sustain=('Normal', 'Vibrato'), release=0.12),
    V('siren', 'Siren', 'Sound Effects', [('Siren', FA + 'Siren.sfz')], kind='perc', max_s=10.0),
    V('ball-whistle', 'Ball Whistle', 'Sound Effects', [('Whistle', EB + 'Ball Whistle.sfz')], kind='perc'),
    V('train-whistle', 'Toy Train Whistle', 'Sound Effects', [('Whistle', EB + 'Train Whistle, Toy.sfz')], kind='perc'),
    # ---------------- mallets, bells, plucked idiophones
    V('glockenspiel', 'Glockenspiel', 'Mallets & Bells', [('Mallet', SI + 'Glockenspiel.sfz')], max_s=6.0, release=1.5),
    V('xylophone', 'Xylophone', 'Mallets & Bells',
      [('Medium Mallets', SI + 'Xylophone - Medium Mallets.sfz'), ('Hard Mallets', SI + 'Xylophone - Hard Mallets.sfz'),
       ('Soft Mallets', SI + 'Xylophone - Soft Mallets.sfz')], max_s=3.0, release=0.6),
    V('marimba', 'Marimba', 'Mallets & Bells', [('Mallet', SI + 'Marimba.sfz')], max_s=5.0, release=0.8),
    V('vibraphone', 'Vibraphone', 'Mallets & Bells',
      [('Soft Mallets', SI + 'Vibraphone - Soft Mallets.sfz'), ('Hard Mallets', SI + 'Vibraphone - Hard Mallets.sfz'),
       ('Bowed', SI + 'Vibraphone - Bowed.sfz')], max_s=8.0, sustain=('Bowed',), release=1.0),
    V('tubular-bells', 'Tubular Bells', 'Mallets & Bells', [('Hit', SI + 'Tubular Bells 1.sfz')], max_s=10.0, release=2.0),
    V('tubular-bells-2', 'Tubular Bells (Bright)', 'Mallets & Bells', [('Hit', SI + 'Tubular Bells 2.sfz')], max_s=10.0, release=2.0),
    V('tubular-glock', 'Tubular Glockenspiel', 'Mallets & Bells', [('Hit', SI + 'Tubular Glockenspiel.sfz')], max_s=8.0, release=1.5),
    V('hand-chimes', 'Hand Chimes', 'Mallets & Bells', [('Ring', SI + 'Hand Chimes.sfz')], max_s=8.0, release=1.5),
    V('wine-glasses', 'Wine Glasses', 'Mallets & Bells',
      [('Slow', FI + 'Wine Glasses - Slow.sfz'), ('Fast', FI + 'Wine Glasses - Fast.sfz')], max_s=8.0, sustain=('Slow', 'Fast'), release=0.8),
    V('balafon', 'Balafon', 'World',
      [('Traditional Mallet', SI + 'Balafon - Traditional Mallet.sfz'), ('Soft Mallet', SI + 'Balafon - Soft Mallet.sfz'),
       ('Hard Mallet', SI + 'Balafon - Hard Mallet.sfz')], max_s=3.0, release=0.5),
    V('kalimba-kenya', 'Kalimba (Kenya)', 'World', [('Pluck', PI + 'Kalimba, Kenya.sfz')], max_s=4.0, release=0.6),
    V('kalimba-tanzania', 'Kalimba (Tanzania)', 'World', [('Pluck', PI + 'Kalimba, Tanzania.sfz')], max_s=4.0, release=0.6),
    V('mbira-gandanga', 'Mbira (Low G)', 'World', [('Pluck', PI + 'Mbira Mavembe (Gandanga), Zimbabwe, Low G.sfz')], max_s=4.0, release=0.6),
    V('mbira-nyamaropa', 'Mbira (Low B)', 'World', [('Pluck', PI + 'Mbira dzaVadzimu Nyamaropa, Zimbabwe, Low B.sfz')], max_s=4.0, release=0.6),
    V('nyunga-nyunga', 'Nyunga Nyunga Mbira', 'World', [('Pluck', PI + 'Nyunga Nyunga, Mozambique, Low F.sfz')], max_s=4.0, release=0.6),
    V('timpani', 'Timpani', 'Percussion', [('Hit', SM + 'Timpani 1 - Hit.sfz'), ('Roll', SM + 'Timpani 1 - Roll.sfz')],
      max_s=6.0, sustain=('Roll',), release=1.0),
    V('timpani-2', 'Timpani (Second Set)', 'Percussion', [('Hit', SM + 'Timpani 2 - Scale.sfz')], max_s=6.0, release=1.0),
    # ---------------- percussion (unpitched, each stroke on its own key)
    V('agogo', 'Agogo Bells', 'Percussion', [('Hits', SI + 'Agogo Bells.sfz')], kind='perc', max_s=3.0),
    V('anvil', 'Anvil', 'Percussion', [('Hits', SI + 'Anvil.sfz')], kind='perc', max_s=4.0),
    V('bell-tree', 'Bell Tree', 'Percussion', [('Strokes', SI + 'Bell Tree - Stroke.sfz'), ('Single Bells', SI + 'Bell Tree - Individual.sfz')], kind='perc', max_s=6.0),
    V('brake-drum', 'Brake Drum', 'Percussion', [('Hits', SI + 'Brake Drum.sfz')], kind='perc', max_s=4.0),
    V('cabasa', 'Cabasa', 'Percussion', [('Hits', SI + 'Cabasa.sfz')], kind='perc', max_s=2.0),
    V('cajon', 'Cajon', 'Percussion', [('Hits', SI + 'Cajon.sfz')], kind='perc', max_s=2.0),
    V('claps', 'Hand Claps', 'Percussion', [('Claps', SI + 'Claps.sfz')], kind='perc', max_s=1.5),
    V('clash-cymbals', 'Clash Cymbals', 'Percussion', [('Crash', SI + 'Clash Cymbals 1.sfz')], kind='perc', max_s=8.0),
    V('clash-cymbals-2', 'Clash Cymbals (Large)', 'Percussion', [('Crash', SI + 'Clash Cymbals 2.sfz')], kind='perc', max_s=8.0),
    V('claves', 'Claves', 'Percussion', [('Hits', SI + 'Claves.sfz')], kind='perc', max_s=1.5),
    V('cowbells', 'Cowbells', 'Percussion', [('Hits', SI + 'Cowbells.sfz')], kind='perc', max_s=2.0),
    V('finger-cymbals', 'Finger Cymbals', 'Percussion', [('Ring', SI + 'Finger Cymbals.sfz')], kind='perc', max_s=5.0),
    V('flexatone', 'Flexatone', 'Sound Effects', [('Shakes', SI + 'Flexatone.sfz')], kind='perc', max_s=4.0),
    V('gong', 'Gong', 'Percussion', [('Hits', SI + 'Gong 1.sfz')], kind='perc', max_s=12.0),
    V('gong-2', 'Gong (Second)', 'Percussion', [('Hits', SI + 'Gong 2.sfz')], kind='perc', max_s=12.0),
    V('guiro', 'Guiro', 'Percussion', [('Scrapes', SI + 'Guiro.sfz')], kind='perc', max_s=2.0),
    V('hand-bells', 'Hand Bells', 'Percussion', [('Ring', SI + 'Hand Bells, Nepalese.sfz')], kind='perc', max_s=6.0),
    V('hi-hat', 'Hi-Hat', 'Percussion', [('Hits', SI + 'Hi-Hat Cymbal.sfz')], kind='perc', max_s=4.0),
    V('mark-tree', 'Mark Tree (Chimes)', 'Percussion', [('Strokes', SI + 'Mark Trees.sfz')], kind='perc', max_s=6.0),
    V('ratchet', 'Ratchet', 'Sound Effects', [('Turns', SI + 'Ratchet.sfz')], kind='perc', max_s=3.0),
    V('shaker-large', 'Shaker (Large)', 'Percussion', [('Shakes', SI + 'Shaker, Large.sfz')], kind='perc', max_s=2.0),
    V('shaker-small', 'Shaker (Small)', 'Percussion', [('Shakes', SI + 'Shaker, Small.sfz')], kind='perc', max_s=2.0),
    V('slapstick', 'Slapstick (Whip)', 'Percussion', [('Hits', SI + 'Slapstick.sfz')], kind='perc', max_s=1.5),
    V('sleigh-bells', 'Sleigh Bells', 'Percussion', [('Shakes', SI + 'Sleigh Bells.sfz')], kind='perc', max_s=3.0),
    V('slit-drum', 'Slit Drum', 'World', [('Hits', SI + 'Slit Drum.sfz')], kind='perc', max_s=2.0),
    V('suspended-cymbal', 'Suspended Cymbal', 'Percussion', [('Strokes', SI + 'Suspended Cymbal 1.sfz')], kind='perc', max_s=10.0),
    V('suspended-cymbal-2', 'Suspended Cymbal (Second)', 'Percussion', [('Strokes', SI + 'Suspended Cymbal 2.sfz')], kind='perc', max_s=10.0),
    V('tambourine', 'Tambourine', 'Percussion', [('Hits', SI + 'Tambourine 1.sfz')], kind='perc', max_s=2.0),
    V('tambourine-2', 'Tambourine (Second)', 'Percussion', [('Hits', SI + 'Tambourine 2.sfz')], kind='perc', max_s=2.0),
    V('triangles', 'Triangles', 'Percussion', [('Hits', SI + 'Triangles.sfz')], kind='perc', max_s=6.0),
    V('vibraslap', 'Vibraslap', 'Percussion', [('Hit', SI + 'Vibraslap.sfz')], kind='perc', max_s=3.0),
    V('woodblock', 'Woodblock', 'Percussion', [('Hits', SI + 'Woodblock.sfz')], kind='perc', max_s=1.5),
    V('ocean-drum', 'Ocean Drum', 'Sound Effects', [('Waves', OM + 'Ocean Drum.sfz')], kind='perc', max_s=8.0),
    V('concert-bass-drum', 'Concert Bass Drum', 'Percussion', [('Hit', SM + 'Bass Drum 1.sfz')], kind='perc', max_s=6.0),
    V('concert-bass-drum-2', 'Concert Bass Drum (Rolls & Rubs)', 'Percussion', [('Strokes', SM + 'Bass Drum 2.sfz')], kind='perc', max_s=8.0),
    V('bongos', 'Bongos', 'World', [('Hits', SM + 'Bongos.sfz')], kind='perc', max_s=2.0),
    V('congas', 'Congas', 'World', [('Hits', SM + 'Conga.sfz')], kind='perc', max_s=2.0),
    V('darbuka', 'Darbuka', 'World', [('Hits', SM + 'Darbuka.sfz')], kind='perc', max_s=2.0),
    V('frame-drum', 'Frame Drum', 'World', [('Hits', SM + 'Frame Drum.sfz')], kind='perc', max_s=3.0),
    V('snare-1', 'Snare Drum A', 'Percussion', [('Hits', SM + 'Snare Drum, Modern 1.sfz')], kind='perc', max_s=4.0),
    V('snare-2', 'Snare Drum B', 'Percussion', [('Hits', SM + 'Snare Drum, Modern 2.sfz')], kind='perc', max_s=4.0),
    V('snare-3', 'Snare Drum C', 'Percussion', [('Hits', SM + 'Snare Drum, Modern 3.sfz')], kind='perc', max_s=4.0),
    V('snare-rope', 'Rope-Tension Snare', 'Percussion', [('Hits', SM + 'Snare Drum, Rope Tension.sfz')], kind='perc', max_s=4.0),
    V('snare-marching', 'Marching Snare', 'Percussion', [('Hits', SM + 'Legacy Snares - drum3_marching.sfz')], kind='perc', max_s=4.0),
    V('tom-high', 'Concert Tom (High)', 'Percussion', [('Hits', SM + 'Tom 1.sfz')], kind='perc', max_s=4.0),
    V('tom-low', 'Concert Tom (Low)', 'Percussion', [('Hits', SM + 'Tom 2.sfz')], kind='perc', max_s=4.0),
]

# VSCO-2-CE (file-name mapped). (articulation, folder)
VS = 'Strings/', 'Brass/', 'Woodwinds/'
VSCO_CATALOG = [
    S('solo-violin', 'Solo Violin', 'Strings',
      [('Sustain', 'Strings/Solo Violin/Arco Vib'), ('Spiccato', 'Strings/Solo Violin/spic'),
       ('Pizzicato', 'Strings/Solo Violin/Pizz'), ('Tremolo', 'Strings/Solo Violin/Trem')],
      sustain=('Sustain', 'Tremolo'), release=0.22),
    S('fiddle', 'Country Fiddle', 'Strings',
      [('Sustain', 'Strings/Solo Violin/Arco Vib'), ('Short', 'Strings/Solo Violin/spic'), ('Pizzicato', 'Strings/Solo Violin/Pizz')],
      sustain=('Sustain',), release=0.16),
    S('violin-section', 'Violin Section', 'Strings',
      [('Sustain', 'Strings/Violin Section/susVib'), ('Spiccato', 'Strings/Violin Section/Spic'),
       ('Pizzicato', 'Strings/Violin Section/Pizz'), ('Tremolo', 'Strings/Violin Section/Trem')],
      sustain=('Sustain', 'Tremolo'), mono=False, release=0.35),
    S('viola-section', 'Viola Section', 'Strings',
      [('Sustain', 'Strings/Viola Section/susvib'), ('Spiccato', 'Strings/Viola Section/spic'),
       ('Pizzicato', 'Strings/Viola Section/pizz'), ('Tremolo', 'Strings/Viola Section/trem')],
      sustain=('Sustain', 'Tremolo'), mono=False, release=0.35),
    S('cello-section', 'Cello Section', 'Strings',
      [('Sustain', 'Strings/Cello Section/susvib'), ('Spiccato', 'Strings/Cello Section/spic'),
       ('Pizzicato', 'Strings/Cello Section/pizzT'), ('Tremolo', 'Strings/Cello Section/trem')],
      sustain=('Sustain', 'Tremolo'), mono=False, release=0.35),
    S('double-bass', 'Double Bass (Solo)', 'Bass',
      [('Pizzicato', 'Strings/Solo Contrabass/Pizz'), ('Sustain', 'Strings/Solo Contrabass/SusVib'),
       ('Sustain Straight', 'Strings/Solo Contrabass/SusNV'), ('Spiccato', 'Strings/Solo Contrabass/Spic'),
       ('Tremolo', 'Strings/Solo Contrabass/Trem')],
      sustain=('Sustain', 'Sustain Straight', 'Tremolo'), release=0.12),
    S('trumpet', 'Trumpet', 'Brass',
      [('Sustain', 'Brass/Trumpet/sus'), ('Vibrato', 'Brass/Trumpet/susvib'), ('Staccato', 'Brass/Trumpet/stac'),
       ('Straight Mute', 'Brass/Trumpet/straightM-sus'), ('Harmon Mute', 'Brass/Trumpet/harmonM-sus')],
      sustain=('Sustain', 'Vibrato', 'Straight Mute', 'Harmon Mute'), release=0.2),
    S('french-horn', 'French Horn', 'Brass',
      [('Sustain', 'Brass/F Horn/sus'), ('Staccato', 'Brass/F Horn/stac'), ('Muted', 'Brass/F Horn/mute')],
      sustain=('Sustain', 'Muted'), release=0.3),
    S('trombone', 'Trombone', 'Brass',
      [('Sustain', 'Brass/Tenor Trombone/sus'), ('Vibrato', 'Brass/Tenor Trombone/vib'), ('Staccato', 'Brass/Tenor Trombone/stac')],
      sustain=('Sustain', 'Vibrato'), release=0.25),
    S('tuba', 'Tuba', 'Brass', [('Sustain', 'Brass/Tuba/sus'), ('Staccato', 'Brass/Tuba/stac')], sustain=('Sustain',), release=0.25),
    S('flute', 'Flute', 'Woodwinds',
      [('Sustain', 'Woodwinds/Flute/susNV'), ('Vibrato', 'Woodwinds/Flute/susvib'), ('Expressive', 'Woodwinds/Flute/expvib'),
       ('Staccato', 'Woodwinds/Flute/stac')], sustain=('Sustain', 'Vibrato', 'Expressive'), release=0.18),
    S('piccolo', 'Piccolo', 'Woodwinds', [('Sustain', 'Woodwinds/Piccolo/Sus'), ('Staccato', 'Woodwinds/Piccolo/Stac')],
      sustain=('Sustain',), release=0.15),
    S('clarinet', 'Clarinet', 'Woodwinds', [('Sustain', 'Woodwinds/Clarinet/susLong'), ('Staccato', 'Woodwinds/Clarinet/stac')],
      sustain=('Sustain',), release=0.18),
    S('oboe', 'Oboe', 'Woodwinds',
      [('Sustain', 'Woodwinds/Oboe/Sus'), ('Vibrato', 'Woodwinds/Oboe/Vib'), ('Staccato', 'Woodwinds/Oboe/Stacc')],
      sustain=('Sustain', 'Vibrato'), release=0.18),
    S('bassoon', 'Bassoon', 'Woodwinds',
      [('Sustain', 'Woodwinds/Bassoon/sus'), ('Vibrato', 'Woodwinds/Bassoon/vib'), ('Staccato', 'Woodwinds/Bassoon/stac')],
      sustain=('Sustain', 'Vibrato'), release=0.2),
]

# String ensemble: the three VSCO sections played as one instrument, split across the keyboard
# (cello below F#3, viola F#3..E4, violins from F4 up).
ENSEMBLE = S('string-ensemble', 'String Ensemble', 'Strings',
             [('Sustain', 'susVib'), ('Spiccato', 'Spic'), ('Pizzicato', 'Pizz'), ('Tremolo', 'Trem')],
             sustain=('Sustain', 'Tremolo'), mono=False, release=0.35)
ENSEMBLE_SPLITS = [('cello-section', 0, 53), ('viola-section', 54, 64), ('violin-section', 65, 127)]

# Drum kits on General MIDI notes. Pieces: GM note -> (source, spec, key in that SFZ or VD piece)
ORCH_KIT = {
    36: ('Bass Drum 1.sfz', 60), 38: ('Snare Drum, Modern 1.sfz', 60), 40: ('Snare Drum, Modern 1.sfz', 61),
    37: ('Snare Drum, Modern 2.sfz', 62), 42: ('Hi-Hat Cymbal.sfz', 42), 44: ('Hi-Hat Cymbal.sfz', 44),
    46: ('Hi-Hat Cymbal.sfz', 46), 41: ('Tom 2.sfz', 62), 43: ('Tom 2.sfz', 64), 45: ('Tom 2.sfz', 62),
    47: ('Tom 1.sfz', 64), 48: ('Tom 1.sfz', 62), 50: ('Tom 1.sfz', 64), 49: ('Clash Cymbals 1.sfz', 61),
    57: ('Suspended Cymbal 1.sfz', 68), 51: ('Suspended Cymbal 2.sfz', 68), 53: ('Suspended Cymbal 2.sfz', 67),
    54: ('Tambourine 1.sfz', 60), 56: ('Cowbells.sfz', 60), 70: ('Shaker, Small.sfz', 61), 75: ('Claves.sfz', 60),
    39: ('Claps.sfz', 60), 76: ('Woodblock.sfz', 60), 81: ('Triangles.sfz', 60), 80: ('Triangles.sfz', 61),
    63: ('Conga.sfz', 61), 62: ('Conga.sfz', 60), 64: ('Conga.sfz', 65), 60: ('Bongos.sfz', 60), 61: ('Bongos.sfz', 63),
    69: ('Cabasa.sfz', 60), 73: ('Guiro.sfz', 60), 58: ('Vibraslap.sfz', 60), 67: ('Agogo Bells.sfz', 60), 68: ('Agogo Bells.sfz', 61),
}

report = []


def log(msg):
    report.append(msg)


# ======================================================================================
# audio helpers
# ======================================================================================
def load(path):
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    x = x[:, :2]
    if sr != OUT_RATE:
        g = math.gcd(int(sr), OUT_RATE)
        x = resample_poly(x, OUT_RATE // g, int(sr) // g, axis=0)
    return x, sr


def trim_tail(x, max_seconds, thresh_db=-60.0):
    """Cut trailing silence (and anything past max_seconds), fade the last 30 ms."""
    if len(x) == 0:
        return x
    mono = np.abs(x).max(axis=1)
    peak = mono.max() + 1e-12
    above = np.nonzero(mono > peak * 10 ** (thresh_db / 20))[0]
    end = min(len(x), (above[-1] + int(0.05 * OUT_RATE)) if len(above) else len(x))
    end = min(end, int(max_seconds * OUT_RATE))
    y = x[:max(end, 64)].copy()
    fade = min(len(y) // 4, int(0.03 * OUT_RATE))
    if fade > 0:
        y[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return y


def trim_head(x, thresh_db=-55.0, keep_ms=3.0):
    """Cut leading silence (keeps a few ms before the onset). Returns (x, frames removed)."""
    mono = np.abs(x).max(axis=1)
    peak = mono.max() + 1e-12
    on = int(np.argmax(mono > peak * 10 ** (thresh_db / 20)))
    start = max(0, on - int(keep_ms * 0.001 * OUT_RATE))
    return x[start:], start


def yin_f0(x, lo=27.0, hi=2500.0):
    """YIN fundamental (Hz) of the steady part of a note; None if unvoiced."""
    m = x.mean(axis=1)
    n = len(m)
    seg = m[int(min(0.25 * OUT_RATE, n * 0.2)): int(min(1.25 * OUT_RATE, n * 0.8))]
    if len(seg) < 4096:
        seg = m[: min(n, 16384)]
    W = min(len(seg) // 2, 8192)
    if W < 1024:
        return None
    tmax = min(int(OUT_RATE / lo), W - 1)
    tmin = max(2, int(OUT_RATE / hi))
    x0 = seg[:W]
    # difference function via FFT (fast): d(tau) = r0 + r_tau(energy) - 2*acf(tau)
    nfft = 1 << int(math.ceil(math.log2(len(seg) + W)))
    F = np.fft.rfft(seg, nfft)
    K = np.fft.rfft(x0[::-1], nfft)
    acf = np.fft.irfft(F * K, nfft)[W - 1: W - 1 + tmax + 1]
    sq = np.concatenate(([0.0], np.cumsum(seg ** 2)))
    e0 = sq[W] - sq[0]
    etau = sq[np.arange(tmax + 1) + W] - sq[np.arange(tmax + 1)]
    d = e0 + etau - 2 * acf
    d[0] = 0.0
    cmnd = np.ones_like(d)
    run = np.cumsum(d[1:])
    cmnd[1:] = d[1:] * np.arange(1, tmax + 1) / np.maximum(run, 1e-20)
    cand = np.nonzero(cmnd[tmin:] < 0.15)[0]
    if len(cand) == 0:
        tau = tmin + int(np.argmin(cmnd[tmin:]))
        if cmnd[tau] > 0.4:
            return None
    else:
        tau = tmin + int(cand[0])
        while tau + 1 <= tmax and cmnd[tau + 1] < cmnd[tau]:
            tau += 1
    if 1 <= tau < tmax:
        a, b, c = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
        den = a - 2 * b + c
        tau = tau + 0.5 * (a - c) / den if den != 0 else tau
    return OUT_RATE / tau


def detected_note(x):
    f0 = yin_f0(x)
    return None if f0 is None else 69 + 12 * math.log2(f0 / 440.0)


def add_loop(x, start_frac=0.35, min_len_s=0.6):
    """Bake a seamless crossfade loop near the end of a sustained sample. Returns (x, loopStart, loopEnd)."""
    n = len(x)
    if n < int(1.5 * OUT_RATE):
        return x, -1, -1
    ls = max(int(n * start_frac), int(0.5 * OUT_RATE))
    le = int(n * 0.88)
    if le - ls < int(min_len_s * OUT_RATE):
        return x, -1, -1
    xf = int(min(0.12 * OUT_RATE, (le - ls) // 3))
    y = x[:le].copy()
    t = np.linspace(0, 1, xf)[:, None]
    y[le - xf: le] = y[le - xf: le] * np.sqrt(1 - t) + x[ls - xf: ls] * np.sqrt(t)
    return y, ls, le


def write_flac(path, x):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    peak = np.abs(x).max() if len(x) else 0.0
    if peak > 0.999:
        x = x * (0.999 / peak)
    sf.write(path, x.astype(np.float32), OUT_RATE, subtype='PCM_24', format='FLAC')


def rms_db(x, seconds=1.0):
    seg = x[: int(seconds * OUT_RATE)]
    if len(seg) == 0:
        return -120.0
    r = math.sqrt(float(np.mean(seg ** 2)) + 1e-20)
    return 20 * math.log10(r)


# ======================================================================================
# SFZ (the subset VCSL uses)
# ======================================================================================
def sfz_note(v):
    v = v.strip()
    if re.fullmatch(r'-?\d+', v):
        return int(v)
    m = re.fullmatch(r'([a-gA-G])([#b]?)(-?\d)', v)
    if not m:
        raise ValueError('bad note ' + v)
    pc = NOTE[m.group(1).upper() + (m.group(2) or '')]
    return pc + 12 * (int(m.group(3)) + 1)


def parse_sfz(path):
    """Returns a list of regions (dicts of opcodes, inherited from <global>/<group>/<control>)."""
    text = open(path, encoding='utf-8', errors='replace').read()
    text = re.sub(r'//[^\n]*', '', text)
    tokens = re.split(r'(<\w+>)', text)
    glob, group, control, cur = {}, {}, {}, None
    regions = []
    header = None
    for tok in tokens:
        if not tok.strip():
            continue
        if re.fullmatch(r'<\w+>', tok):
            if header == 'region' and cur is not None:
                regions.append(cur)
            header = tok[1:-1]
            if header == 'group':
                group = {}
            elif header == 'global':
                glob = {}
            elif header == 'region':
                cur = {}
            continue
        # opcodes: key=value pairs; sample= may contain spaces up to the next opcode
        for m in re.finditer(r'(\w+)=(.*?)(?=\s+\w+=|\s*$)', tok.strip(), re.S):
            k, v = m.group(1), m.group(2).strip()
            if header == 'region':
                cur[k] = v
            elif header == 'group':
                group[k] = v
            elif header == 'global':
                glob[k] = v
            elif header == 'control':
                control[k] = v
        if header == 'region':
            cur = {**control, **glob, **group, **cur}
    if header == 'region' and cur is not None:
        regions.append(cur)
    return regions


# ======================================================================================
# zone writing
# ======================================================================================
class Builder:
    def __init__(self, out_root, spec):
        self.spec = spec
        self.dir = os.path.join(out_root, folder_of(spec))
        os.makedirs(self.dir, exist_ok=True)
        self.files = {}      # (source path, offset) -> (rel file, frames, loop, tune, onset level)
        self.count = 0

    def sample(self, src, offset_frames_src, src_rate_hint, loop, retune, pitched_root=None):
        """Loads, trims, optionally loops and writes one sample. Returns its record (cached)."""
        key = (src, offset_frames_src, loop, retune)
        if key in self.files:
            return self.files[key]
        x, sr = load(src)
        if offset_frames_src > 0:
            off = int(round(offset_frames_src * OUT_RATE / sr))
            x = x[off:]
        if len(x) < 32:
            raise RuntimeError('empty sample ' + src)
        x, _ = trim_head(x)
        x = trim_tail(x, self.spec['max_s'])
        tune = 0.0
        det = None
        if pitched_root is not None:
            det = detected_note(x)
            if retune and det is not None:
                off = (det - pitched_root) - 12 * round((det - pitched_root) / 12)
                if abs(off) <= 0.5:
                    tune = round(-off * 100.0, 1)
        ls = le = -1
        if loop:
            x, ls, le = add_loop(x)
        self.count += 1
        rel = f"s{self.count:04d}.flac"
        write_flac(os.path.join(self.dir, rel), x)
        rec = dict(file=rel, frames=int(len(x)), loop=[ls, le], tune=tune, level=rms_db(x, 0.6), det=det)
        self.files[key] = rec
        return rec


def folder_of(spec):
    fam = spec['family'].lower().replace(' & ', '-').replace(' ', '-')
    return f"{fam}/{spec['id']}"


def zone_record(rec, root, lo, hi, vlo, vhi, db, rr, rrn, keytrack, xin=None, xout=None, extra_tune=0.0):
    z = {"file": rec['file'], "frames": rec['frames'], "root": int(root), "lo": int(lo), "hi": int(hi),
         "vlo": int(vlo), "vhi": int(vhi), "db": round(float(db), 2), "tune": round(rec['tune'] + extra_tune, 1),
         "rr": int(rr), "rrn": int(rrn), "keytrack": bool(keytrack)}
    if rec['loop'][0] >= 0:
        z["loop"] = rec['loop']
    if xin is not None:
        z["xin"] = xin
    if xout is not None:
        z["xout"] = xout
    return z


_sample_index = {}


def find_moved_sample(root, rel):
    """A few VCSL sample folders sit one level off from where their .sfz points (for example the
    psaltery inside 'Harpsichord, Italian'). Finds the file by its last path parts anywhere in VCSL."""
    if root not in _sample_index:
        idx = {}
        for dirpath, _, files in os.walk(root):
            for f in files:
                if f.lower().endswith(('.wav', '.flac')):
                    full = os.path.join(dirpath, f)
                    parts = full.replace('\\', '/').split('/')
                    idx.setdefault('/'.join(parts[-3:]).lower(), full)
        _sample_index[root] = idx
    key = '/'.join(rel.split('/')[-3:]).lower()
    return _sample_index[root].get(key)


def build_sfz_articulation(b, vcsl_root, art_name, sfz_rel, spec):
    path = os.path.join(vcsl_root, sfz_rel)
    regions = parse_sfz(path)
    base = os.path.dirname(path)
    zones, rel_zones = [], []
    art_release = None
    loop = art_name in spec['sustain']
    pitched = spec['kind'] == 'pitched'
    for r in regions:
        if 'sample' not in r:
            continue
        if any(k.startswith('sw_') for k in r):     # keyswitch maps are skipped (we build our own)
            continue
        if 'on_locc64' in r or 'on_hicc64' in r:    # pedal-noise samples (triggered by the pedal, not keys)
            continue
        src = os.path.join(base, r.get('default_path', ''), r['sample'].replace('\\', '/'))
        if not os.path.exists(src):
            moved = find_moved_sample(vcsl_root, r['sample'].replace('\\', '/'))
            if moved is None:
                log(f"{spec['id']}: missing {src}")
                continue
            src = moved
        lo = sfz_note(r.get('lokey', r.get('key', '0')))
        hi = sfz_note(r.get('hikey', r.get('key', '127')))
        if hi < lo:
            continue
        pedal = None                                 # sustain-pedal condition of the sample set
        if 'locc64' in r or 'hicc64' in r:
            pedal = 1 if int(r.get('locc64', '0')) >= 64 else (0 if int(r.get('hicc64', '127')) <= 64 else None)
        root = sfz_note(r.get('pitch_keycenter', r.get('key', str(lo))))
        root -= int(r.get('transpose', '0'))
        vlo, vhi = int(r.get('lovel', '0')), int(r.get('hivel', '127'))
        db = float(r.get('volume', '0'))
        tune = float(r.get('tune', '0'))
        rrn = int(r.get('seq_length', '1'))
        rr = int(r.get('seq_position', '1'))
        keytrack = float(r.get('pitch_keytrack', '100')) != 0.0 and pitched
        offset = int(r.get('offset', '0'))
        if 'ampeg_release' in r:
            art_release = float(r['ampeg_release'])
        xin = xout = None
        if 'xfin_lovel' in r or 'xfin_hivel' in r:
            xin = [int(r.get('xfin_lovel', vlo)), int(r.get('xfin_hivel', vlo))]
        if 'xfout_lovel' in r or 'xfout_hivel' in r:
            xout = [int(r.get('xfout_lovel', vhi)), int(r.get('xfout_hivel', vhi))]
        is_release = r.get('trigger', 'attack') == 'release'
        rec = b.sample(src, offset, None, loop and not is_release, spec['tune'] and pitched, root if pitched else None)
        z = zone_record(rec, root, lo, hi, vlo, vhi, db, rr, rrn, keytrack, xin, xout, extra_tune=tune)
        if pedal is not None:
            z['pedal'] = pedal
        if 'ampeg_release' in r and not is_release:
            z['release'] = round(min(float(r['ampeg_release']), 12.0), 3)
        (rel_zones if is_release else zones).append(z)
    if not zones:
        raise RuntimeError(f"{spec['id']}: no zones in {sfz_rel}")
    if spec['kind'] == 'perc' and spec.get('every_key'):
        pass
    rel = spec['release'] if spec['release'] is not None else (art_release if art_release is not None else 0.3)
    if spec['kind'] == 'perc':
        rel = min(rel, 2.0)
    art = {"name": art_name, "release": round(min(rel, 8.0), 3), "loop": loop, "zones": zones}
    if rel_zones:
        art["releaseZones"] = rel_zones
    return art


NAME_NOTE = re.compile(r'(?:^|_)([A-G][#b]?)(-?\d)(?=_|\.|[a-z])')


def parse_vsco_name(f):
    """(note letter, octave, layer token, rr) from a VSCO file name."""
    m = NAME_NOTE.search(f)
    if not m:
        return None
    rest = f[m.end():]
    layer = None
    mv = re.search(r'_v(\d+)', rest)
    if mv:
        layer = int(mv.group(1))
    else:
        md = re.search(r'_(ppp|pp|p|mp|mf|f|ff|fff)(?=_|\.|$)', rest)
        if md:
            layer = 100 + DYN.index(md.group(1))
    rr = 1
    mr = re.search(r'_[rR][rR](\d+)', rest)
    if mr:
        rr = int(mr.group(1))
    else:
        mt = re.search(r'(\d+)(?:_[A-Za-z]+)?\.wav$', rest)
        if mt and (mv is None or mt.start() > mv.end()):
            rr = int(mt.group(1))
    return m.group(1), int(m.group(2)), layer if layer is not None else 1, rr


def build_vsco_articulation(b, vsco_root, art_name, folder, spec):
    d = os.path.join(vsco_root, folder)
    loop = art_name in spec['sustain']
    files = []
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith('.wav'):
            continue
        p = parse_vsco_name(f)
        if p is None:
            log(f"{spec['id']}/{art_name}: no note in name {f} - skipped")
            continue
        letter, octv, layer, rr = p
        files.append((os.path.join(d, f), NOTE[letter] + 12 * (octv + 1), layer, rr))
    if not files:
        raise RuntimeError(f"{spec['id']}: no files in {folder}")
    # Octave numbering differs between folders: take it from what the recordings measure.
    measured = []
    loaded = []
    for path, note, layer, rr in files:
        x, _ = load(path)
        x, _ = trim_head(x)
        det = detected_note(trim_tail(x, 3.0))
        loaded.append((path, note, layer, rr, det))
        if det is not None:
            measured.append(round((det - note) / 12))
    shift = 12 * max(set(measured), key=measured.count) if measured else 0
    if shift:
        log(f"{spec['id']}/{art_name}: file names are {shift // 12:+d} octave(s) off the measured pitch - corrected")
    layers = sorted({l for _, _, l, _, _ in loaded})
    nl = len(layers)
    rr_counts = {}
    for path, note, layer, rr, det in loaded:
        rr_counts[(note, layer)] = max(rr_counts.get((note, layer), 0), rr)
    recs = []
    dropped = 0
    for path, note, layer, rr, det in loaded:
        root = note + shift
        if det is not None:
            off = (det - root) - 12 * round((det - root) / 12)
            if abs(off) > 0.5:
                dropped += 1
                log(f"{spec['id']}/{art_name}: {os.path.basename(path)} measures {off:+.2f} semitones off note {root} - left out")
                continue
        rec = b.sample(path, 0, None, loop, True, root)
        recs.append((rec, root, layers.index(layer), rr, rr_counts[(note, layer)]))
    roots = sorted({r[1] for r in recs})
    zones = []
    for rec, root, li, rr, rrn in recs:
        idx = roots.index(root)
        lo = 0 if idx == 0 else (roots[idx - 1] + root) // 2 + 1
        hi = 127 if idx == len(roots) - 1 else (root + roots[idx + 1]) // 2
        # keep the outer zones within a fifth of the last recorded note
        if idx == 0:
            lo = max(lo, root - 7)
        if idx == len(roots) - 1:
            hi = min(hi, root + 7)
        vlo = int(round(li * 128 / nl))
        vhi = int(round((li + 1) * 128 / nl)) - 1
        zones.append(zone_record(rec, root, lo, hi, vlo, vhi, 0.0, rr, rrn, True))
    by_root = {}
    for z in zones:
        by_root.setdefault(z['root'], []).append(z)
    for root, zs in by_root.items():
        bands = sorted({(z['vlo'], z['vhi']) for z in zs})
        if not bands:
            continue
        new_edges = {}
        for i, (vlo, vhi) in enumerate(bands):
            nlo = 0 if i == 0 else (bands[i - 1][1] + vlo + 1) // 2
            nhi = 127 if i == len(bands) - 1 else (vhi + bands[i + 1][0] - 1) // 2
            new_edges[(vlo, vhi)] = (nlo, nhi)
        for i, (vlo, vhi) in enumerate(bands):
            if i > 0:
                new_edges[(vlo, vhi)] = (new_edges[bands[i - 1]][1] + 1, new_edges[(vlo, vhi)][1])
        for z in zs:
            z['vlo'], z['vhi'] = new_edges[(z['vlo'], z['vhi'])]
    rel = spec['release'] if spec['release'] is not None else 0.25
    if art_name in ('Staccato', 'Spiccato', 'Short', 'Pizzicato'):
        rel = min(rel, 0.15)
    log(f"{spec['id']}/{art_name}: {len(zones)} samples, notes {roots[0]}..{roots[-1]}, {nl} layer(s)"
        + (f", {dropped} left out" if dropped else ""))
    return {"name": art_name, "release": round(rel, 3), "loop": loop, "zones": zones}


def calibrate(spec, arts):
    """Instrument gain so a mid-loud note in the middle of the range plays at about -20 dBFS RMS
    (percussion: the main hit at about -16 dBFS over its first 0.3 s)."""
    target = -20.0 if spec['kind'] == 'pitched' else -16.0
    zs = arts[0]['zones']
    if not zs:
        return 0.0
    roots = sorted(z['root'] for z in zs)
    mid = roots[len(roots) // 2]
    near = [z for z in zs if abs(z['root'] - mid) <= 7] or zs
    vtop = max(z['vhi'] for z in near)
    loudish = [z for z in near if z['vlo'] <= 100 <= z['vhi']] or [z for z in near if z['vhi'] == vtop]
    levels = [z['_level'] + z['db'] for z in loudish if '_level' in z]
    if not levels:
        return 0.0
    g = target - float(np.median(levels))
    return round(max(-24.0, min(24.0, g)), 2)


def finish_instrument(spec, arts, b, source):
    for a in arts:
        for z in a['zones']:
            for rec in b.files.values():
                if rec['file'] == z['file']:
                    z['_level'] = rec['level']
                    break
    gain = calibrate(spec, arts)
    for a in arts:
        for z in a['zones'] + a.get('releaseZones', []):
            z.pop('_level', None)
    lo = min(z['lo'] for z in arts[0]['zones'])
    hi = max(z['hi'] for z in arts[0]['zones'])
    seat = SEATS.get(spec['family'], (0.0, 0.5, 0.25))
    inst = {
        "format": "hem-instrument", "version": LIB_VERSION,
        "id": spec['id'], "name": spec['name'], "family": spec['family'], "kind": spec['kind'],
        "source": source, "licence": "CC0 1.0", "credit": CREDITS[source],
        "gainDb": gain, "velTrack": spec['vel_track'], "mono": spec['mono'], "range": [lo, hi],
        "seat": {"x": seat[0], "depth": seat[1], "height": seat[2]},
        "sampleRate": OUT_RATE, "articulations": arts,
    }
    with open(os.path.join(b.dir, 'instrument.json'), 'w', encoding='utf-8') as f:
        json.dump(inst, f, indent=1)
    nz = sum(len(a['zones']) + len(a.get('releaseZones', [])) for a in arts)
    return f"{spec['id']}: {len(arts)} articulation(s), {nz} zones, {b.count} files, range {lo}..{hi}, gain {gain:+.1f} dB"


def build_ensemble(b, vsco_root, spec):
    sections = {x['id']: x for x in VSCO_CATALOG}
    arts = []
    for art_name, _ in spec['arts']:
        zones = []
        for sid, lo, hi in ENSEMBLE_SPLITS:
            sec = sections[sid]
            folder = dict(sec['arts']).get(art_name)
            if folder is None:
                raise RuntimeError(f"{sid} has no {art_name}")
            a = build_vsco_articulation(b, vsco_root, art_name, folder, dict(sec, sustain=spec['sustain']))
            for z in a['zones']:
                zlo, zhi = max(z['lo'], lo), min(z['hi'], hi)
                if zlo > zhi:
                    continue
                z['lo'], z['hi'] = zlo, zhi
                zones.append(z)
        # the outer section zones were kept within a fifth of their last note; stretch the
        # zones next to a split so no key between sections is left silent
        covered = sorted({k for z in zones for k in range(z['lo'], z['hi'] + 1)})
        for k in range(covered[0], covered[-1] + 1):
            if k not in covered:
                near = min(zones, key=lambda z: min(abs(z['lo'] - k), abs(z['hi'] - k)))
                for z in zones:
                    if z['root'] == near['root']:
                        z['lo'], z['hi'] = min(z['lo'], k), max(z['hi'], k)
                covered = sorted({q for z in zones for q in range(z['lo'], z['hi'] + 1)})
        rel = spec['release'] if art_name not in ('Spiccato', 'Pizzicato') else 0.15
        arts.append({"name": art_name, "release": rel, "loop": art_name in spec['sustain'], "zones": zones})
        log(f"{spec['id']}/{art_name}: {len(zones)} zones from the three sections")
    # samples outside every section's split were written but are not played: remove them
    used = {z['file'] for a in arts for z in a['zones']}
    for rec in list(b.files.values()):
        if rec['file'] not in used:
            os.remove(os.path.join(b.dir, rec['file']))
    b.count = len(used)
    return finish_instrument(spec, arts, b, 'vsco')


def build_one(job):
    kind, spec, roots, out = job
    report.clear()
    try:
        b = Builder(out, spec)
        if kind == 'vcsl':
            arts = [build_sfz_articulation(b, roots['vcsl'], n, p, spec) for n, p in spec['arts']]
            msg = finish_instrument(spec, arts, b, 'vcsl')
        elif kind == 'vsco':
            arts = [build_vsco_articulation(b, roots['vsco'], n, p, spec) for n, p in spec['arts']]
            msg = finish_instrument(spec, arts, b, 'vsco')
        elif kind == 'ensemble':
            msg = build_ensemble(b, roots['vsco'], spec)
        elif kind == 'kit-orch':
            msg = build_orch_kit(b, roots['vcsl'], spec)
        elif kind == 'kit-vd':
            msg = build_vd_kit(b, roots['vd'], spec)
        else:
            raise RuntimeError('unknown job ' + kind)
        return True, msg, report[:]
    except Exception as e:
        return False, f"{spec['id']}: FAILED - {e}\n{traceback.format_exc()}", report[:]


# ======================================================================================
# drum kits
# ======================================================================================
def find_sfz(vcsl_root, name):
    for dirpath, _, files in os.walk(vcsl_root):
        if name in files:
            return os.path.join(dirpath, name)
    raise RuntimeError('sfz not found: ' + name)


def build_orch_kit(b, vcsl_root, spec):
    zones = []
    cache = {}
    for gm, (sfz, key) in sorted(ORCH_KIT.items()):
        if sfz not in cache:
            p = find_sfz(vcsl_root, sfz)
            cache[sfz] = (p, parse_sfz(p))
        p, regions = cache[sfz]
        base = os.path.dirname(p)
        hits = [r for r in regions if 'sample' in r and not any(k.startswith('sw_') for k in r)
                and sfz_note(r.get('lokey', '0')) <= key <= sfz_note(r.get('hikey', '127'))
                and r.get('trigger', 'attack') != 'release']
        if not hits:
            log(f"orchestral kit: nothing on key {key} of {sfz} for GM {gm}")
            continue
        for r in hits:
            src = os.path.join(base, r['sample'].replace('\\', '/'))
            rec = b.sample(src, int(r.get('offset', '0')), None, False, False, None)
            zones.append(zone_record(rec, gm, gm, gm, int(r.get('lovel', '0')), int(r.get('hivel', '127')),
                                     float(r.get('volume', '0')), int(r.get('seq_position', '1')),
                                     int(r.get('seq_length', '1')), False))
            zones[-1]['_level'] = rec['level']
    arts = [{"name": "Kit", "release": 0.8, "loop": False, "zones": zones}]
    return finish_kit(spec, arts, b, 'vcsl')


def build_vd_kit(b, vd_root, spec):
    mid = os.path.join(vd_root, "Samples/mid")
    kick_close = os.path.join(vd_root, "Samples/kickmic")
    snare_close = os.path.join(vd_root, "Samples/snaremic")
    pieces = [  # GM note, folder, articulation, close mic folder, close mic blend
        (36, "kick", "snon", kick_close, 0.8), (35, "kick", "snon", kick_close, 0.8),
        (38, "snare", "center", snare_close, 0.6), (40, "snare", "center", snare_close, 0.6),
        (37, "snare", "crossstick", snare_close, 0.5),
        (42, "hh", "closed", None, 0), (44, "hh", "pedal", None, 0), (46, "hh", "open", None, 0),
        (51, "ride", "ride", None, 0), (59, "ride", "ride", None, 0), (53, "ride", "bell", None, 0),
        (49, "crash", "crash", None, 0), (57, "flatride", "crash", None, 0),
        (41, "ltom", "center", None, 0), (43, "ltom", "center", None, 0), (45, "ltom", "center", None, 0),
        (47, "htom", "center", None, 0), (48, "htom", "center", None, 0), (50, "htom", "center", None, 0),
    ]
    zones = []
    written = {}
    for note, folder, art, close, blend in pieces:
        d = os.path.join(mid, folder)
        if not os.path.isdir(d):
            log(f"jazz kit: missing {d}")
            continue
        hits = []
        for f in sorted(os.listdir(d)):
            m = re.match(rf"mid_{folder}_{art}_vl(\d+)(?:_rr(\d+))?\.flac$", f)
            if not m:
                continue
            key = (folder, art, f)
            if key not in written:
                x, _ = load(os.path.join(d, f))
                if close:
                    cf = os.path.join(close, folder, f.replace("mid_", os.path.basename(close) + "_"))
                    if os.path.exists(cf):
                        c, _ = load(cf)
                        n = min(len(x), len(c))
                        x = x[:n] + blend * c[:n]
                x, _ = trim_head(x)
                x = trim_tail(x, 4.0 if folder in ("crash", "ride", "flatride") or art == "open" else 2.0)
                b.count += 1
                rel = f"s{b.count:04d}.flac"
                write_flac(os.path.join(b.dir, rel), x)
                loud = float(np.sqrt(np.mean(x[: int(0.05 * OUT_RATE)] ** 2)))
                written[key] = (loud, dict(file=rel, frames=len(x), loop=[-1, -1], tune=0.0, level=rms_db(x, 0.3), det=None))
            hits.append(written[key])
        if not hits:
            continue
        hits.sort(key=lambda h: h[0])
        per = 4 if len(hits) >= 8 else max(1, len(hits) // 2)
        layers = [hits[i:i + per] for i in range(0, len(hits), per)]
        # pitch the doubled toms apart (41/43/45 low, 47/48/50 high) by a couple of semitones
        tom_shift = {41: -2.0, 43: -1.0, 45: 0.0, 47: -1.0, 48: 0.0, 50: 1.5}.get(note, 0.0)
        for li, layer in enumerate(layers):
            vlo = int(round(li * 128 / len(layers)))
            vhi = int(round((li + 1) * 128 / len(layers))) - 1
            for rr, (loud, rec) in enumerate(layer, 1):
                z = zone_record(rec, note, note, note, vlo, vhi, 0.0, rr, len(layer), False, extra_tune=tom_shift * 100.0)
                z['_level'] = rec['level']
                zones.append(z)
    arts = [{"name": "Kit", "release": 0.6, "loop": False, "zones": zones}]
    return finish_kit(spec, arts, b, 'vd')


def finish_kit(spec, arts, b, source):
    # level: kick + snare at mid velocity around -16 dBFS (first 0.3 s)
    zs = [z for z in arts[0]['zones'] if z['root'] in (36, 38) and z['vlo'] <= 90 <= z['vhi']]
    levels = [z['_level'] + z['db'] for z in zs]
    gain = round(max(-24.0, min(24.0, -16.0 - float(np.median(levels)))), 2) if levels else 0.0
    for z in arts[0]['zones']:
        z.pop('_level', None)
    seat = SEATS['Drum Kits']
    inst = {
        "format": "hem-instrument", "version": LIB_VERSION,
        "id": spec['id'], "name": spec['name'], "family": "Drum Kits", "kind": "drums",
        "source": source, "licence": "CC0 1.0", "credit": CREDITS[source],
        "gainDb": gain, "velTrack": 0.7, "mono": False, "range": [35, 81],
        "seat": {"x": seat[0], "depth": seat[1], "height": seat[2]},
        "sampleRate": OUT_RATE, "articulations": arts,
    }
    with open(os.path.join(b.dir, 'instrument.json'), 'w', encoding='utf-8') as f:
        json.dump(inst, f, indent=1)
    notes = sorted({z['root'] for z in arts[0]['zones']})
    return f"{spec['id']}: drum kit, {len(arts[0]['zones'])} zones on GM notes {notes}, gain {gain:+.1f} dB"


# ======================================================================================
# index
# ======================================================================================
FAMILY_ORDER = ['Keys', 'Guitars & Plucked', 'Bass', 'Strings', 'Brass', 'Woodwinds', 'Folk & Free Reeds',
                'Mallets & Bells', 'Drum Kits', 'Percussion', 'World', 'Synths', 'Choir & Pads', 'Sound Effects']


def write_index(out):
    items = []
    for dirpath, _, files in os.walk(out):
        if 'instrument.json' not in files:
            continue
        p = os.path.join(dirpath, 'instrument.json')
        with open(p, encoding='utf-8') as f:
            inst = json.load(f)
        size = sum(os.path.getsize(os.path.join(dirpath, x)) for x in os.listdir(dirpath))
        items.append({
            "id": inst['id'], "name": inst['name'], "family": inst['family'], "kind": inst['kind'],
            "folder": os.path.relpath(dirpath, out).replace('\\', '/'), "source": inst['source'],
            "licence": inst['licence'], "range": inst['range'],
            "articulations": [a['name'] for a in inst['articulations']],
            "zones": sum(len(a['zones']) for a in inst['articulations']), "bytes": size,
        })
    fam_rank = {f: i for i, f in enumerate(FAMILY_ORDER)}
    items.sort(key=lambda i: (fam_rank.get(i['family'], 99), i['name']))
    idx = {"format": "hem-instrument-library", "version": LIB_VERSION, "name": "HEM Instrument Library",
           "sampleRate": OUT_RATE, "families": [f for f in FAMILY_ORDER if any(i['family'] == f for i in items)],
           "instruments": items}
    with open(os.path.join(out, 'index.json'), 'w', encoding='utf-8') as f:
        json.dump(idx, f, indent=1)
    total = sum(i['bytes'] for i in items)
    print(f"index: {len(items)} instruments, {total / 1e9:.2f} GB")
    return idx


def write_notices(out, roots):
    with open(os.path.join(out, 'CREDITS.txt'), 'w', encoding='utf-8') as c:
        c.write("HEM Instrument Library - recordings used by HEM Instrument Forge\n\n")
        for k in ('vcsl', 'vsco', 'vd', 'hem'):
            c.write(CREDITS[k] + "\n\n")
        c.write("All recorded samples were trimmed, resampled to 48 kHz, levelled and (for held notes) given "
                "crossfade loops by Tools/build_library.py.\n")
    with open(os.path.join(out, 'LICENSES.txt'), 'w', encoding='utf-8') as l:
        for name, key in (("Versilian Community Sample Library", 'vcsl'), ("VS Chamber Orchestra 2: Community Edition", 'vsco'),
                          ("Virtuosity Drums", 'vd')):
            p = os.path.join(roots.get(key) or '', 'LICENSE')
            txt = open(p, encoding='utf-8', errors='replace').read() if roots.get(key) and os.path.exists(p) else "CC0 1.0 Universal"
            l.write(f"===== {name} =====\n{txt}\n\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vcsl'); ap.add_argument('--vsco'); ap.add_argument('--vd')
    ap.add_argument('--models'); ap.add_argument('--out'); ap.add_argument('--only')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2)))
    ap.add_argument('--index')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    if a.list:
        for s in CATALOG + VSCO_CATALOG + [ENSEMBLE]:
            print(s['id'], '|', s['name'], '|', s['family'])
        return
    if a.index:
        write_index(a.index)
        return
    out = a.out
    os.makedirs(out, exist_ok=True)
    roots = {'vcsl': a.vcsl, 'vsco': a.vsco, 'vd': a.vd}
    only = set(a.only.split(',')) if a.only else None
    jobs = []
    if a.vcsl:
        jobs += [('vcsl', s, roots, out) for s in CATALOG]
        jobs.append(('kit-orch', dict(id='kit-orchestral', name='Orchestral Kit', family='Drum Kits', max_s=8.0), roots, out))
    if a.vsco:
        jobs += [('vsco', s, roots, out) for s in VSCO_CATALOG]
        jobs.append(('ensemble', ENSEMBLE, roots, out))
    if a.vd:
        jobs.append(('kit-vd', dict(id='kit-jazz', name='Jazz Kit', family='Drum Kits', max_s=4.0), roots, out))
    if only:
        jobs = [j for j in jobs if j[1]['id'] in only]
    ok_all = True
    lines = []
    with Pool(a.jobs) as pool:
        for ok, msg, rep in pool.imap_unordered(build_one, jobs):
            print(msg, flush=True)
            lines.append(msg)
            lines += rep
            ok_all &= ok
    if a.models:
        for dirpath, _, files in os.walk(a.models):
            if 'instrument.json' in files:
                rel = os.path.relpath(dirpath, a.models)
                dest = os.path.join(out, rel)
                if os.path.exists(dest):
                    shutil.rmtree(dest)
                shutil.copytree(dirpath, dest)
        lines.append("modeled instruments copied from " + a.models)
    write_notices(out, roots)
    write_index(out)
    with open(os.path.join(out, 'BUILD-REPORT.txt'), 'w', encoding='utf-8') as r:
        r.write("\n".join(lines) + "\n")
    if not ok_all:
        print("SOME INSTRUMENTS FAILED - see BUILD-REPORT.txt", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
