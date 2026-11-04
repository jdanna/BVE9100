# OBS Studio link

`obs/bve9100_obs.py` is an OBS Studio script that connects OBS to a running emulator:

* **Media files as tape decks.** Each deck's media source follows its virtual VTR frame by frame. PLAY rolls the
  file. STILL, STOP, JOG, SHTL, FF, REW and cueing park it on the frame the deck is on. Pre-rolls and edits run
  it in sync with the editor. The deck's start timecode (01:00:00:00 by default) is the first frame of the file.
* **The editor drives OBS's switcher.** Every take of the virtual switcher switches OBS's program to the scene
  mapped to that crosspoint, with the matching transition and length:
  * CUT uses Cut.
  * DISS uses Fade.
  * WIPE uses Wipe, or Fade when OBS has no wipe transition.

  That covers live source keys and the cuts, dissolves and wipes of PREVIEW, single edits and AUTO REC. In
  studio mode, OBS's preview follows the switcher's preset bus.
* **Deck audio follows the editor.** You hear the decks on program, both decks during a transition, and during
  an audio-only edit the source decks rolling in it. The other decks are muted.
* **Recording the record tape.** OBS records each edit the editor makes, and the segments are conformed into one
  file. See [Recording the tape](#recording-the-tape).
* **Optional extras:**
  * A text source per deck can show its role, timecode and mode, as a timecode window.
  * Scene changes you make in OBS can go back to the switcher's program bus.
  * OBS hotkeys can run each deck's PLAY / STILL / STOP / FF / REW and the switcher's CUT / AUTO.

It needs only OBS's built-in Python scripting. Recording the tape also needs ffmpeg and ffprobe. It was tested
with OBS 30 (Ubuntu package) and OBS 33 (Flatpak, which includes Python, ffmpeg and ffprobe).

## Setting it up

1. Start the emulator (`npm start`, port 8090). A new machine has its decks assigned; on an older one, assign them once (README, "The decks").
2. In OBS: **Tools > Scripts**. Under Python Settings, point OBS at a Python 3 install if it hasn't found one.
   Add `obs/bve9100_obs.py` with **+**.
3. In the script's settings, choose a media file for each deck (port A = R, B = P1, C = P2, D = P3).
4. Press **Create deck scenes**. This makes one scene per deck, "BVE R", "BVE P1" and so on, each with a media
   source, and maps crosspoints 1-4 to them (the editor's default XPT ASSIGN: R = 1, P1 = 2, P2 = 3, P3 = 4).
   Decks that already have a media source picked keep it. It also makes:
   * a scene called "BVE audio" that holds every deck's media source outside the picture and is nested in each
     deck scene. OBS only mixes the audio of sources that are on program, so this keeps every deck audible while
     the script mutes and unmutes them;
   * empty scenes "BVE AX1", "BVE AX2" and "BVE BLACK" on crosspoints 8, 9 and 10, for the editor's AUX1, AUX2
     and BLACK sources. Put anything in them: a camera, graphics, another media file. An empty scene is black.
     The emulator gives AX1, AX2 and BLK those crosspoints when they are unassigned (INIT > ASSIGN, second page,
     VIDEO EFF).

To use your own scenes instead, pick a media source for each deck and a scene for each crosspoint by hand. A
crosspoint scene can be anything: a camera, a composition, or a scene that contains the deck's media source.

| Setting | |
|---|---|
| Emulator URL | `http://127.0.0.1:8090`, or the port you started it on |
| Port X media source / start timecode | Which media source plays deck X, and the timecode of the file's first frame |
| Port X timecode text source | A text source to show `P1  01:00:12:03  PLAY` |
| Switcher port | `E` |
| Program follows the switcher | Takes switch OBS's program |
| Program delay (frames) | How far OBS's program runs behind the editor (default 2). See [Timing](#timing) |
| Studio mode preview follows the preset bus | |
| Scene changes in OBS go to the switcher | A scene picked in OBS is sent as `pgm N` for its crosspoint |
| Crosspoint N scene | The OBS scene for switcher crosspoint N |
| Deck audio follows the editor | Mutes the decks the editor is not listening to |
| Record deck port | `A` (R) |
| Tape folder | Where the segment list (`tape.json`) and exports go (default `~/BVE-9100 tape`) |
| Export folder, ffmpeg | Another folder for exports; the ffmpeg command (ffprobe is looked for next to it) |
| Export format | ProRes 422 HQ with 24-bit PCM (`.mov`), or H.264 with AAC (`.mp4`) |
| Export audio | A1-A4 as four tracks, or mixed to one stereo track |
| Line-up correction (frames) | Shifts where a segment's frames are taken from its recording (default 0) |
| Toolbar port | The port the emulator's toolbar talks to (8091) |
| Log activity | Logs each deck roll and park, each take and each recording's line-up in the script log |

## Recording the tape

OBS cannot insert-edit into a recording, so the tape is kept as a list of segments. Each segment is one OBS
recording plus the stretch of the record tape it covers and the tracks it holds (V, A1-A4). Export lays the
segments over each other in the order they were recorded and conforms the result into one file. A later
segment only replaces the tracks it was recorded with, so an audio-only insert leaves the picture alone.
Parts of the tape that nothing was recorded on are black and silent.

The emulator's page shows an **OBS TAPE** toolbar in its top bar while the script is loaded:

| | |
|---|---|
| Lamp | Dark red when armed, bright red while OBS records |
| **Record edits** | Arms recording. While armed, OBS starts recording when an edit is cued and stops when the edit ends |
| Info | Number of segments and the length of the tape (hover for the list) |
| **Undo** | Removes the last segment. Its recording file is kept |
| **Clear** | Empties the tape after asking. The recordings are kept |
| **Export** | Conforms the tape into `BVE tape <date>.mov` (or `.mp4`) with the record tape's start timecode, and shows progress |

The same buttons are in the script's settings. A manual REC on the record deck (R) is recorded as a segment
with all tracks.

Use OBS's recording settings for the recording format. A format that keeps the whole file when OBS stops
writing it (MKV) is safest. The output resolution and frame rate are those of OBS; set OBS to 29.97 fps
(Settings > Video > Common FPS 29.97) to match the tape. Export conforms other rates to 29.97.

To line each recording up, the script notes when OBS rendered each frame while the record deck ran, and counts
the recording's frames afterwards with ffprobe. If the export is a frame early or late on every source, change
**Line-up correction**. It applies to segments recorded afterwards.

The toolbar talks to the script on 127.0.0.1:8091. It answers only local pages:

```sh
curl http://127.0.0.1:8091/tape                      # state, segments, export progress
curl -X POST -d on http://127.0.0.1:8091/tape/arm    # on / off
curl -X POST http://127.0.0.1:8091/tape/export       # also /tape/undo, /tape/clear
```

## How it works

A thread in the script polls `GET /devices` about 30 times a second. A 33 ms OBS timer applies the state:

* **Deck in PLAY or REC at normal speed, and inside the file:** the media plays. If it drifts more than 300 ms
  from the deck, it is re-seeked.
* **Any other deck mode:** the media is paused and seeked to the deck's frame. The position is
  `(frame - start) × 1001/30` ms.

The deck positions are extrapolated from the last poll to the moment they are used.

Each switcher take carries its type, from, to and length. When a dissolve or wipe starts in the same frame as a
cut to its FROM source, which is how an edit's in point arrives, the script first cuts OBS to the FROM scene and
then runs the transition.

The calls that switch scenes and transitions wait for OBS's UI thread. OBS's Python bindings keep the interpreter
lock during every call, so making these calls from a script timer can freeze OBS when another Python callback
wants the UI thread. The script makes them from its own thread through ctypes, which releases the lock while it
waits.

The same device API works without OBS:

```sh
curl http://127.0.0.1:8090/devices                         # decks and switcher (take counter, timeline, events)
curl -X POST -d "trans wipe" http://127.0.0.1:8090/devices/E
curl -X POST -d "pst 3"      http://127.0.0.1:8090/devices/E
curl -X POST -d "auto 15"    http://127.0.0.1:8090/devices/E  # 15-frame wipe to crosspoint 3
```

The web version runs in the browser and has no HTTP port, so the OBS link works with the desktop build only.

## Timing

A media source's picture comes out about 2 frames after the deck it follows. Seeking cannot take that out: OBS
drops a seek made as the source starts playing, and a seek while it plays costs more time than it gains. So the
rest of the program runs with the same delay, as a video processor's delay would be handled. Takes, the audio
follow and the timecode windows follow the editor **Program delay** frames late. Takes are sent one frame early,
because a scene switch takes about a frame to show. A recorded segment is lined up with the same delay.

Measured on OBS 33 at 29.97 fps against burnt-in timecode, with a text source updated every frame as the
reference: the cut lands on the in point, and the media, the switcher and the recorded tape agree to the frame.
They agree both when a deck rolls mid-file and when it starts on the file's first frame. If your system's media
picture is a frame later or earlier, change **Program delay**.

## Flatpak OBS

The Flatpak OBS can reach the emulator on 127.0.0.1 and read files in your home folder. It has its own Python,
ffmpeg and ffprobe, so leave **ffmpeg** at `ffmpeg`. Its settings live in
`~/.var/app/com.obsproject.Studio/config/obs-studio`.
