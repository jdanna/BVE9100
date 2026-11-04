"""BVE-9100 link for OBS Studio (Tools > Scripts > +).

Connects OBS to the BVE-9100 emulator's device API (GET /devices, POST /devices/<port>) so that

  * media sources act as the editor's tape decks: each deck's media follows its virtual VTR (play, still, stop,
    jog / shuttle / cue as scrubbing, pre-rolls and edits) frame for frame, the deck's start timecode being the
    first frame of the file;
  * the editor drives the OBS switcher: every take of the virtual switcher (source keys with MONITOR SELECT = PGM,
    and the cuts, dissolves and wipes of previews and edits, on the edit timeline) switches OBS's program to the
    scene mapped to that crosspoint, with the matching OBS transition and duration; in studio mode the preview
    follows the switcher's preset bus;
  * deck audio follows the editor: the decks not heard are muted (all deck sources are kept live in a 'BVE audio'
    scene nested in each deck scene, as OBS only mixes the audio of sources on program);
  * the record tape: with recording armed (the OBS TAPE toolbar on the emulator's page, served by this script on
    127.0.0.1:8091), OBS records every edit the editor makes as a segment (tape timecodes and tracks), and Export
    conforms the segments into one file with ffmpeg, later segments replacing only the tracks they were recorded on;
  * optional: text sources show a deck's timecode and mode (a burnt-in timecode window), OBS scene changes made by
    hand are sent back to the switcher, and OBS hotkeys run the decks' transports and the switcher's CUT / AUTO.

"Create deck scenes" builds a scene per deck with a media source for the file chosen for that deck, and maps the
crosspoints to them the way the editor's XPT ASSIGN defaults are set up (1 = R, 2 = P1, 3 = P2, 4 = P3), plus empty
scenes 'BVE AX1', 'BVE AX2' and 'BVE BLACK' on crosspoints 8-10 for anything else.

OBS's program runs PROGRAM_DELAY frames behind the editor, the time a media source's picture takes to come out, so
that picture, takes and the recorded tape stay frame-accurate with each other.

Needs OBS's built-in Python scripting (Python 3 configured under Tools > Scripts > Python Settings); recording the
tape also needs ffmpeg and ffprobe.
"""
import ctypes
import datetime
import http.server
import json
import os
import queue
import subprocess
import threading
import time
import urllib.parse
import urllib.request

import obspython as obs

PORTS = "ABCDEFGH"
DEFAULT_ROLES = {"A": "R", "B": "P1", "C": "P2", "D": "P3", "E": "P4", "F": "P5", "G": "P6", "H": "P7"}
NXPT = 12
FRAME_MS = 1001.0 / 30.0           # one NTSC frame; timecode counts 30 frames per second at 29.97 per second
# Timing, measured against burnt-in timecode in recordings (OBS 33, 29.97), with a text source updated every tick as
# the reference: the polled deck position is extrapolated to the moment of use.
PROGRAM_DELAY = 2                   # OBS's program runs this many frames behind the editor: a media source's picture
                                    # trails its deck by about 2 frames (and no seek moves it), so the takes, the audio
                                    # follow and the timecode windows are delayed to match, and the tape lines up with it
TAKE_LATENCY = 1                    # a scene switch shows about a frame after it is asked for (OBS's UI thread)
DRIFT_MS = 300                      # rolling media further than this from its deck is re-seeked
TAPE_ALIGN = 0                      # record tape: tape frame a = what OBS rendered while the record deck was on it
TRANSITIONS = {"cut": "cut_transition", "mix": "fade_transition", "wipe": "wipe_transition"}


class UiCalls:
    """OBS frontend calls that switch scenes and transitions, made from a worker thread of this script.

    These calls wait for OBS's UI thread. OBS's Python bindings keep the interpreter lock through every call, so making
    them from the script's timer (OBS's video thread) would hold the lock while the UI thread waits -- and freeze OBS the
    moment any Python callback (from this or another script) wants to run on the UI thread. So they run here, through
    ctypes, which lets go of the interpreter lock for the duration of the call."""

    NAMES = (None, "libobs-frontend-api.so.30", "libobs-frontend-api.so.0", "libobs-frontend-api.so",
             "obs-frontend-api.dll", "libobs-frontend-api.1.dylib", "libobs-frontend-api.dylib")

    def __init__(self):
        self.lib = None
        for n in self.NAMES:
            try:
                lib = ctypes.CDLL(n)
                lib.obs_frontend_set_current_scene
            except (OSError, AttributeError):
                continue
            for f in ("obs_frontend_set_current_scene", "obs_frontend_set_current_preview_scene",
                      "obs_frontend_set_current_transition"):
                getattr(lib, f).argtypes, getattr(lib, f).restype = [ctypes.c_void_p], None
            for f in ("obs_frontend_recording_start", "obs_frontend_recording_stop"):
                getattr(lib, f).argtypes, getattr(lib, f).restype = [], None
            try:
                lib.obs_frontend_set_transition_duration.argtypes = [ctypes.c_int]
                lib.obs_frontend_set_transition_duration.restype = None
            except AttributeError:
                pass
            self.lib = lib
            break
        self.obslib = None
        for n in (None, "libobs.so.30", "libobs.so.0", "libobs.so", "obs.dll", "libobs.framework/libobs", "libobs.0.dylib"):
            try:
                lib = ctypes.CDLL(n)
                lib.obs_source_set_muted.argtypes, lib.obs_source_set_muted.restype = [ctypes.c_void_p, ctypes.c_bool], None
            except (OSError, AttributeError):
                continue
            self.obslib = lib
            break
        self.q = queue.Queue()
        threading.Thread(target=self.run, daemon=True).start()

    @staticmethod
    def ptr(o):
        try:
            return int(o)
        except TypeError:
            return int(o.this)

    def run(self):
        while True:
            job = self.q.get()
            try:
                job()
            except Exception as e:
                link.msgs.append((obs.LOG_WARNING, "OBS call failed: %s" % e))

    def set_muted(self, src, muted):
        """obs_source_set_muted, on the worker thread: made from the script's timer it can freeze OBS. Releases src."""
        def job():
            try:
                if self.obslib is not None:
                    self.obslib.obs_source_set_muted(self.ptr(src), bool(muted))
                else:
                    obs.obs_source_set_muted(src, muted)
            finally:
                obs.obs_source_release(src)
        self.q.put(job)

    def call(self, name, *sources_or_values, release=()):
        """Queue frontend function `name` with these arguments (sources are passed as pointers); the sources in
        `release` are released once it has run."""
        def job():
            try:
                if self.lib is not None:
                    args = [v if isinstance(v, int) else self.ptr(v) for v in sources_or_values]
                    getattr(self.lib, name)(*args)
                elif hasattr(obs, name):
                    getattr(obs, name)(*sources_or_values)
            finally:
                for r in release:
                    obs.obs_source_release(r)
        self.q.put(job)


class Link:
    def __init__(self):
        self.url = "http://127.0.0.1:8090"
        self.enabled = True
        self.decks = {}                # port -> {"source", "start", "text", "file"}
        self.sw_port = "E"
        self.xpt = {}                  # crosspoint -> scene name
        self.follow_switcher = True
        self.preview_follows_pst = True
        self.obs_drives_switcher = False
        self.devices = None            # last /devices answer (list), None while not connected
        self.devices_t = 0.0           # when it arrived (time.monotonic)
        self.devices_ns = 0            # the same on OBS's clock (os_gettime_ns)
        self.err_t = {}
        self.delay = PROGRAM_DELAY     # frames (setting)
        self.late = []                 # (devices_ns, by_port) polls waiting out the program delay
        self.late_ns = 0
        self.connected = False
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = None
        self.last_take = None
        self.last_scene_set = None     # scene name we switched to (to tell our scene changes from the user's)
        self.last_scene_seen = None
        self.last_pst = None
        self.seek = {}                 # port -> (time of last seek, target ms)
        self.text_last = {}
        self.hotkeys = {}              # name -> hotkey id
        self.verbose = False
        self.audio_follow = True
        self.scene_decks = {}          # scene name -> deck ports whose media source is in it
        self.muted = {}                # port -> muted by route_audio
        self.rolling = {}              # port -> media rolling (for the activity log)
        self.msgs = []                 # log lines from the polling thread, written out on OBS's thread

    def log(self, msg, level=None):
        obs.script_log(obs.LOG_INFO if level is None else level, msg)

    # ------------------------------------------------------------------ polling thread
    def start(self):
        self.stop.clear()
        if self.thread is None or not self.thread.is_alive():
            self.thread = threading.Thread(target=self.poll, daemon=True)
            self.thread.start()

    def poll(self):
        while not self.stop.is_set():
            try:
                with urllib.request.urlopen(self.url.rstrip("/") + "/devices", timeout=0.5) as r:
                    data = json.loads(r.read().decode())
                with self.lock:
                    self.devices = data
                    self.devices_t = time.monotonic()
                    self.devices_ns = obs.os_gettime_ns()
                if not self.connected:
                    self.msgs.append((obs.LOG_INFO, "connected to the BVE-9100 at %s" % self.url))
                self.connected = True
                self.stop.wait(0.03)
            except Exception as e:
                if self.connected:
                    self.msgs.append((obs.LOG_WARNING, "lost the BVE-9100 at %s (%s)" % (self.url, e)))
                self.connected = False
                with self.lock:
                    self.devices = None
                self.stop.wait(1.0)

    def post(self, port, body):
        def send():
            try:
                req = urllib.request.Request(self.url.rstrip("/") + "/devices/" + port, data=body.encode(), method="POST")
                urllib.request.urlopen(req, timeout=1).read()
            except Exception as e:
                self.msgs.append((obs.LOG_WARNING, "POST /devices/%s %s failed: %s" % (port, body, e)))
        threading.Thread(target=send, daemon=True).start()

    # ------------------------------------------------------------------ applied on OBS's thread
    def tick(self):
        while self.msgs:
            level, msg = self.msgs.pop(0)
            self.log(msg, level)
        if not self.enabled:
            return
        with self.lock:
            devs = self.devices
        by_port = {d.get("port"): d for d in devs or []}
        rec = by_port.get(tape.rec_port)
        tape.tick(rec if rec and rec.get("kind") == "vtr" else None, self.devices_ns)
        if not devs:
            return
        # the switcher and the audio follow the editor self.delay frames late, in step with the media's picture (asked
        # for TAKE_LATENCY frames earlier, as a switch takes that long to show)
        if self.devices_ns != self.late_ns:
            self.late_ns = self.devices_ns
            self.late.append((self.devices_ns, by_port))
        due = obs.os_gettime_ns() - max(0, self.delay - TAKE_LATENCY) * FRAME_MS * 1e6
        late = None
        while self.late and self.late[0][0] <= due:
            late = self.late.pop(0)[1]
        if late is not None:
            self.late_by_port = late
        late = getattr(self, "late_by_port", None) or {}
        for port, cfg in self.decks.items():
            dev = by_port.get(port)
            if not dev or dev.get("kind") != "vtr":
                continue
            if cfg["source"]:
                self.sync_deck(port, cfg, dev)
            if cfg["text"]:
                self.show_tc(cfg, dev)
        sw = late.get(self.sw_port)
        if sw and sw.get("kind") == "switcher":
            self.follow(sw)
            if self.audio_follow:
                self.route_audio(sw, late)
        self.watch_obs_scene()

    def deck_ports(self, xpt):
        """Deck ports whose media source is directly in the scene of this crosspoint."""
        name = self.xpt.get(xpt)
        if not name:
            return set()
        if name not in self.scene_decks:
            ports = set()
            sc = obs.obs_get_source_by_name(name)
            if sc is not None:
                scene = obs.obs_scene_from_source(sc)
                for p, cfg in self.decks.items():
                    if cfg["source"] and scene is not None and obs.obs_scene_find_source(scene, cfg["source"]) is not None:
                        ports.add(p)
                obs.obs_source_release(sc)
            self.scene_decks[name] = ports
        return self.scene_decks[name]

    def route_audio(self, sw, by_port):
        """Audio follows the editor: the decks on program (both while a transition runs) and, during the record pass
        of an audio-only edit, the source decks rolling in it."""
        hear = set(self.deck_ports(sw.get("pgm")))
        if sw.get("in_transition"):
            take = sw.get("take") or {}
            hear |= self.deck_ports(take.get("from")) | self.deck_ports(take.get("to"))
        rec = by_port.get(tape.rec_port) or {}
        r = rec.get("rec") or {}
        if rec.get("mode") == "rec" and r.get("to", 0) < 0 and not (r.get("tracks", 0x7c) & 0x04):
            hear = {p for p, d in by_port.items() if p != tape.rec_port and d.get("kind") == "vtr"
                    and (d.get("edit") or {}).get("active") and d.get("mode") == "play"}
        for p, cfg in self.decks.items():
            if not cfg["source"]:
                continue
            mute = p not in hear
            if self.muted.get(p) == mute:
                continue
            src = obs.obs_get_source_by_name(cfg["source"])
            if src is not None:
                ui.set_muted(src, mute)
                self.muted[p] = mute

    def sync_deck(self, port, cfg, dev):
        src = obs.obs_get_source_by_name(cfg["source"])
        if src is None:
            return
        try:
            mode, speed = dev.get("mode"), float(dev.get("speed", 0))
            frame = float(dev["frame"]) if "frame" in dev else tc_frames(dev.get("tc", ""))
            if mode in ("play", "rec") and self.devices_ns:
                # the deck has moved on since it was polled: where it is now
                frame += speed * TC_FPS * (obs.os_gettime_ns() - self.devices_ns) / 1e9
            dur = obs.obs_source_media_get_duration(src)
            target = (frame - cfg["start"]) * FRAME_MS
            inside = target >= 0 and (dur <= 0 or target < dur)
            target = max(0.0, min(target, dur - FRAME_MS) if dur > 0 else target)
            state = obs.obs_source_media_get_state(src)
            cur = obs.obs_source_media_get_time(src)
            now = time.monotonic()
            last_t, last_target = self.seek.get(port, (0.0, -1e9))
            rolling = mode in ("play", "rec") and abs(speed - 1.0) < 0.01 and inside
            if self.verbose and self.rolling.get(port) != rolling:
                self.log("deck %s: %s %s at %s -> media %s at %.2f s" % (port, cfg["source"], "rolls" if rolling else "parks",
                                                                   dev.get("tc"), "play" if rolling else "still", target / 1000.0))
            self.rolling[port] = rolling
            if rolling:
                # A media source's picture trails the deck by about PROGRAM_DELAY frames once it rolls; no seek changes
                # that (one made as it starts playing is dropped, one made while it plays costs more time than it
                # gains), so the rest of the program is delayed to match and only a real drift is corrected here.
                if state in (obs.OBS_MEDIA_STATE_STOPPED, obs.OBS_MEDIA_STATE_ENDED, obs.OBS_MEDIA_STATE_NONE):
                    obs.obs_source_media_restart(src)
                    obs.obs_source_media_set_time(src, int(target))
                    self.seek[port] = (now, target)
                elif state != obs.OBS_MEDIA_STATE_PLAYING:
                    obs.obs_source_media_play_pause(src, False)
                    self.seek[port] = (now, target)
                elif abs(cur - target) > DRIFT_MS and now - last_t > 1.0:      # drifted: re-lock
                    obs.obs_source_media_set_time(src, int(target))
                    self.seek[port] = (now, target)
                elif self.verbose and now - self.err_t.get(port, 0) > 1.0 and now - last_t > 0.5:
                    self.err_t[port] = now
                    self.log("deck %s: media reports %+.1f frames from the deck" % (port, (cur - target) / FRAME_MS))
            else:
                if state == obs.OBS_MEDIA_STATE_PLAYING:
                    obs.obs_source_media_play_pause(src, True)
                if state in (obs.OBS_MEDIA_STATE_STOPPED, obs.OBS_MEDIA_STATE_ENDED, obs.OBS_MEDIA_STATE_NONE):
                    obs.obs_source_media_restart(src)
                    obs.obs_source_media_play_pause(src, True)
                # still / jog / shuttle / FF / REW / cueing: show the frame the deck is on (seek at most every 60 ms)
                if abs(target - last_target) >= FRAME_MS * 0.5 and now - last_t > 0.06:
                    obs.obs_source_media_set_time(src, int(target))
                    self.seek[port] = (now, target)
                elif abs(cur - target) > FRAME_MS * 1.5 and now - last_t > 0.3:
                    # not on the deck's frame: a pause or seek made while the source was still opening is lost (it
                    # then plays on while reporting paused), so pause and seek again
                    obs.obs_source_media_play_pause(src, True)
                    obs.obs_source_media_set_time(src, int(target))
                    self.seek[port] = (now, target)
        finally:
            obs.obs_source_release(src)

    def show_tc(self, cfg, dev):
        tc = dev.get("tc", "")
        if dev.get("mode") in ("play", "rec") and "frame" in dev and self.devices_ns:
            # where the deck is now (not when it was polled), less the program delay
            tc = tc_str(int(float(dev["frame"]) + float(dev.get("speed", 0)) * TC_FPS *
                            (obs.os_gettime_ns() - self.devices_ns) / 1e9) - self.delay)
        text = "%s  %s  %s" % (cfg["role"], tc, dev.get("mode", "").upper())
        if self.text_last.get(cfg["text"]) == text:
            return
        src = obs.obs_get_source_by_name(cfg["text"])
        if src is None:
            return
        s = obs.obs_data_create()
        obs.obs_data_set_string(s, "text", text)
        obs.obs_source_update(src, s)
        obs.obs_data_release(s)
        obs.obs_source_release(src)
        self.text_last[cfg["text"]] = text

    def follow(self, sw):
        take = sw.get("take") or {}
        n = take.get("n")
        if self.last_take is None:                 # first contact: show the switcher's program as it is
            self.last_take = n
            if self.follow_switcher:
                self.switch(sw.get("pgm"), "cut", 0)
        elif n != self.last_take:
            self.last_take = n
            if self.follow_switcher:
                kind = take.get("type", "cut")
                # a dissolve or wipe starts from the source the switcher had on program, which may have been cut to
                # in the same frame (an edit's FROM source at the in point): put it on air first
                force = False
                if kind != "cut" and self.xpt.get(take.get("from")) and self.xpt.get(take.get("from")) != self.on_air():
                    self.switch(take.get("from"), "cut", 0, force=True)
                    force = True
                self.switch(take.get("to"), kind, take.get("frames", 0), force=force)
        if self.preview_follows_pst and obs.obs_frontend_preview_program_mode_active():
            pst = sw.get("pst")
            if pst != self.last_pst:
                self.last_pst = pst
                name = self.xpt.get(pst)
                scene = obs.obs_get_source_by_name(name) if name else None
                if scene is not None:
                    ui.call("obs_frontend_set_current_preview_scene", scene, release=(scene,))

    def on_air(self):
        cur = obs.obs_frontend_get_current_scene()
        if cur is None:
            return None
        name = obs.obs_source_get_name(cur)
        obs.obs_source_release(cur)
        return name

    def switch(self, xpt, kind, frames, force=False):
        name = self.xpt.get(xpt)
        if not name:
            if self.verbose:
                self.log("switcher: %s to crosspoint %s, which has no scene in OBS" % (kind, xpt))
            return
        if not force and name == self.on_air():
            return
        scene = obs.obs_get_source_by_name(name)
        if scene is None:
            return
        ms = int(round((frames or 0) * FRAME_MS))
        want = TRANSITIONS.get(kind if ms > 0 else "cut", "cut_transition")
        trans = obs.obs_frontend_get_transitions()
        pick = None
        for t in trans:
            if obs.obs_source_get_id(t) == want:
                pick = t
                break
        if pick is None and want != "cut_transition":    # no wipe set up in OBS: dissolve instead
            for t in trans:
                if obs.obs_source_get_id(t) == "fade_transition":
                    pick = t
                    break
        if pick is not None:
            pick = obs.obs_source_get_ref(pick)
        obs.source_list_release(trans)
        if pick is not None:
            ui.call("obs_frontend_set_current_transition", pick, release=(pick,))
        if ms > 0:
            ui.call("obs_frontend_set_transition_duration", ms)
        if self.verbose:
            self.log("switcher: %s to crosspoint %s (%s), %d ms" % (kind, xpt, name, ms))
        self.last_scene_set = name
        ui.call("obs_frontend_set_current_scene", scene, release=(scene,))

    def watch_obs_scene(self):
        # program scene changed in OBS by hand: send it to the switcher's program bus. Polled here rather than taken
        # from a frontend event callback: a Python callback on OBS's UI thread can deadlock against this timer,
        # which holds the interpreter lock while it waits for the UI thread to switch scenes.
        name = self.on_air()
        if name is None or name == self.last_scene_seen:
            return
        self.last_scene_seen = name
        if name == self.last_scene_set or not self.obs_drives_switcher:
            return
        for x, n in sorted(self.xpt.items()):
            if n == name:
                self.last_scene_set = name
                if self.verbose:
                    self.log("OBS scene %s -> switcher program crosspoint %d" % (name, x))
                self.post(self.sw_port, "pgm %d" % x)
                return


# ---------------------------------------------------------------------- segmented recording (the record tape)
TC_FPS = 30000.0 / 1001.0           # tape frames per second
TRACKS = (("V", 0x04), ("A1", 0x08), ("A2", 0x10), ("A3", 0x20), ("A4", 0x40))
AUDIO_BITS = 0x78


def tc_str(f):
    f = int(round(f))
    return "%02d:%02d:%02d:%02d" % (f // 108000 % 24, f // 1800 % 60, f // 30 % 60, f % 30)


def track_names(bits):
    return " ".join(n for n, b in TRACKS if bits & b)


class Tape:
    """The record deck's tape as a list of segments. OBS can only append to a file, so each edit is recorded as its own
    OBS recording (from the cue to the end of the post-roll), lined up with the recorder's timecode, and kept with the
    recorder in / out points and the tracks the edit recorded. Later segments cover earlier ones on their own tracks;
    export conforms the whole tape into one file with ffmpeg."""

    def __init__(self):
        self.lock = threading.RLock()
        self.dir = os.path.join(os.path.expanduser("~"), "BVE-9100 tape")
        self.export_dir = ""
        self.ffmpeg = "ffmpeg"
        self.fmt = "prores"
        self.audio = "tracks"
        self.align = 0
        self.rec_port = "A"
        self.http_port = 8091
        self.armed = True
        self.segments = []
        self.next_id = 1
        self.state = "idle"            # idle, starting, recording, stopping
        self.take = None
        self.obs_recording = False
        self.export = {"state": "idle", "progress": 0.0, "file": "", "error": ""}
        self.server = None
        self.warned = False
        self.loaded_dir = None
        self.verbose_align = True

    # ---------------------------------------------------------------- persistence
    def path(self):
        return os.path.join(self.dir, "tape.json")

    def load(self):
        with self.lock:
            if self.loaded_dir == self.dir:
                return
            self.loaded_dir = self.dir
            self.segments, self.next_id, self.armed = [], 1, True
            try:
                with open(self.path()) as f:
                    d = json.load(f)
                self.segments = d.get("segments", [])
                self.armed = d.get("armed", True)
                self.next_id = max([s["id"] for s in self.segments] + [0]) + 1
            except FileNotFoundError:
                pass
            except Exception as e:
                link.msgs.append((obs.LOG_WARNING, "tape: cannot read %s (%s)" % (self.path(), e)))

    def save(self):
        with self.lock:
            try:
                os.makedirs(self.dir, exist_ok=True)
                tmp = self.path() + ".tmp"
                with open(tmp, "w") as f:
                    json.dump({"armed": self.armed, "segments": self.segments}, f, indent=1)
                os.replace(tmp, self.path())
            except Exception as e:
                link.msgs.append((obs.LOG_WARNING, "tape: cannot write %s (%s)" % (self.path(), e)))

    # ---------------------------------------------------------------- recording, on OBS's thread
    def tick(self, dev, dev_ns):
        self.obs_recording = obs.obs_frontend_recording_active()
        now = time.monotonic()
        ed = (dev or {}).get("edit") or {}
        rec = (dev or {}).get("rec") or {}
        in_edit = bool(ed.get("armed") and ed.get("active"))
        manual = bool(dev and dev.get("mode") == "rec" and rec.get("manual"))
        if self.state == "idle":
            if not (self.armed and (in_edit or manual)):
                self.warned = False
                return
            if self.obs_recording:
                if not self.warned:
                    link.log("tape: OBS is already recording; this edit is not recorded on the tape", obs.LOG_WARNING)
                    self.warned = True
                return
            self.take = {"rec_n": rec.get("n", 0) - (1 if manual else 0), "samples": [], "recs": {}, "t0": now,
                         "last_frame": None}
            ui.call("obs_frontend_recording_start")
            self.state = "starting"
            link.log("tape: %s, starting the OBS recording" % ("manual REC" if manual else "edit cueing"))
            return
        take = self.take
        if dev and rec.get("n", 0) > take["rec_n"]:
            take["recs"][rec["n"]] = dict(rec)
        if self.state == "starting":
            if self.obs_recording:
                self.state = "recording"
            elif now - take["t0"] > 15:
                link.log("tape: the OBS recording did not start", obs.LOG_WARNING)
                self.state, self.take = "idle", None
            return
        if self.state == "recording":
            if not self.obs_recording:                     # stopped by hand
                self.state = "stopping"
                return
            # Line-up: pair the recorder's position with OBS's render clock. The frame rendered in this cycle is stamped
            # obs_get_video_frame_time(); the deck's position is extrapolated from when /devices answered to that time.
            # samples hold the render time of tape frame 0 (ns), so tape frame a was on screen at base + a / TC_FPS.
            vt = obs.obs_get_video_frame_time()
            if dev and dev.get("mode") in ("play", "rec") and abs(float(dev.get("speed", 0)) - 1) < 0.01:
                f = float(dev.get("frame", 0)) + TC_FPS * (vt - dev_ns) / 1e9
                take["samples"].append(vt - f * 1e9 / TC_FPS)
                take["last_frame"] = f
            take["vt"] = vt
            if not self.armed or not dev or not (in_edit or dev.get("mode") == "rec"):
                # stop the recording output here, at a known time on OBS's clock: the file then holds the frames
                # rendered before it, which anchors the file's frames to the render clock (see finish)
                out = obs.obs_frontend_get_recording_output()
                take["stop_ns"] = obs.os_gettime_ns()
                if out is not None:
                    obs.obs_output_stop(out)
                    obs.obs_output_release(out)
                else:
                    ui.call("obs_frontend_recording_stop")
                self.state = "stopping"
            return
        if self.state == "stopping" and not self.obs_recording:
            self.finish(take)
            self.state, self.take = "idle", None

    @staticmethod
    def out_fps():
        vi = obs.obs_video_info()
        if obs.obs_get_video_info(vi) and vi.fps_den:
            return vi.fps_num / float(vi.fps_den)
        return TC_FPS

    def probe(self, path):
        """(video frames, audio duration in seconds) of a recording, from ffprobe."""
        frames, adur = None, None
        try:
            r = subprocess.run([self.tool("ffprobe"), "-v", "error", "-select_streams", "v:0", "-count_packets",
                                "-show_entries", "stream=nb_read_packets", "-of", "csv=p=0", path],
                               capture_output=True, text=True, timeout=120)
            frames = int(r.stdout.strip().split()[0])
        except Exception:
            pass
        try:
            r = subprocess.run([self.tool("ffprobe"), "-v", "error", "-select_streams", "a:0", "-show_entries",
                                "packet=pts_time,duration_time", "-of", "csv=p=0", path],
                               capture_output=True, text=True, timeout=120)
            last = [l for l in r.stdout.split() if l.strip()][-1].split(",")
            adur = float(last[0]) + float(last[1])
        except Exception:
            pass
        return frames, adur

    def finish(self, take):
        path = obs.obs_frontend_get_last_recording()
        if not take["recs"]:
            link.log("tape: nothing was recorded (no record pass); OBS file %s not used" % path)
            return
        if len(take["samples"]) < 5 or "stop_ns" not in take:
            link.log("tape: too few timing samples to line up %s; not used" % path, obs.LOG_WARNING)
            return
        s = sorted(take["samples"])
        base = s[len(s) // 2]                            # render time of tape frame 0, ns (median)
        spread = (s[int(len(s) * 0.9)] - s[int(len(s) * 0.1)]) / 1e9 * TC_FPS
        fps = self.out_fps()
        step = 1e9 / fps
        frames, adur = self.probe(path)
        if not frames:
            link.log("tape: cannot read %s with ffprobe (%s); not used" % (path, self.tool("ffprobe")), obs.LOG_WARNING)
            return
        # The file's last frame is the last one rendered before the stop: on the render clock's frame grid (through
        # take["vt"]) just below stop_ns. Its first frame is frames - 1 steps before that.
        stop, vt = take["stop_ns"], take["vt"]
        last = vt + ((stop - 1 - vt) // step) * step
        first_v = last - (frames - 1) * step
        first = first_v
        first_a = stop - adur * 1e9 if adur else None
        # file time (s) of tape frame a: (base + a / TC_FPS - first) / 1e9; kept as offset = (base - first) / 1e9
        offset = (base - first) / 1e9
        if self.verbose_align:
            link.log("tape: line-up %s: %d frames, video start %.1f ms, audio start %s ms before the base" % (
                os.path.basename(path), frames, (first_v - base) / 1e6, "%.1f" % ((first_a - base) / 1e6) if first_a else "-"))
        start_frame = int(-(offset + self.align / fps) * TC_FPS) + 1   # first tape frame inside the file
        made = []
        with self.lock:
            for n in sorted(take["recs"]):
                r = take["recs"][n]
                a = max(int(r["from"]), start_frame)
                b = int(r["to"]) if r.get("to", -1) >= 0 else int(take["last_frame"] or a)
                if b <= a:
                    continue
                seg = {"id": self.next_id, "file": path, "in": a, "out": b, "tracks": int(r.get("tracks", 0x7c)) or 0x7c,
                       "offset": offset, "fps": fps, "align": self.align, "manual": bool(r.get("manual")),
                       "created": datetime.datetime.now().isoformat(timespec="seconds")}
                self.next_id += 1
                self.segments.append(seg)
                made.append(seg)
        self.save()
        for seg in made:
            link.log("tape: segment %d  %s - %s  %s  (%s, timing spread %.1f frames)" % (
                seg["id"], tc_str(seg["in"]), tc_str(seg["out"]), track_names(seg["tracks"]), os.path.basename(path), spread))

    # ---------------------------------------------------------------- toolbar commands (any thread)
    def set_armed(self, on):
        with self.lock:
            self.armed = bool(on)
        self.save()

    def undo(self):
        with self.lock:
            if self.segments:
                seg = self.segments.pop()
                link.msgs.append((obs.LOG_INFO, "tape: removed segment %d (the OBS file is kept)" % seg["id"]))
        self.save()

    def clear(self):
        with self.lock:
            self.segments = []
        self.save()
        link.msgs.append((obs.LOG_INFO, "tape: cleared (the OBS files are kept)"))

    def status(self):
        with self.lock:
            segs = [{"id": s["id"], "in": tc_str(s["in"]), "out": tc_str(s["out"]), "frames": s["out"] - s["in"],
                     "tracks": track_names(s["tracks"]), "file": os.path.basename(s["file"])} for s in self.segments]
            span = None
            if self.segments:
                a = min(s["in"] for s in self.segments)
                b = max(s["out"] for s in self.segments)
                span = {"in": tc_str(a), "out": tc_str(b), "length": tc_str(b - a)}
            return {"armed": self.armed, "state": self.state, "connected": link.connected, "obs_recording": self.obs_recording,
                    "segments": segs, "span": span, "export": dict(self.export), "folder": self.dir}

    # ---------------------------------------------------------------- export
    @staticmethod
    def resolve(segs, bits):
        """Pieces (a, b, segment) of the tape on a track mask, in tape order: later segments cover earlier ones."""
        pieces = []
        for seg in segs:
            if not seg["tracks"] & bits:
                continue
            a, b = seg["in"], seg["out"]
            keep = []
            for x, y, s in pieces:
                if y <= a or x >= b:
                    keep.append((x, y, s))
                else:
                    if x < a:
                        keep.append((x, a, s))
                    if y > b:
                        keep.append((b, y, s))
            keep.append((a, b, seg))
            pieces = sorted(keep, key=lambda p: p[0])
        return pieces

    def fill(self, pieces, start, end):
        out, at = [], start
        for x, y, s in pieces:
            if x > at:
                out.append((at, x, None))
            out.append((x, y, s))
            at = y
        if at < end:
            out.append((at, end, None))
        return out

    def tool(self, name):
        if name == "ffprobe":
            d, base = os.path.split(self.ffmpeg)
            return os.path.join(d, base.replace("ffmpeg", "ffprobe")) if "ffmpeg" in base else "ffprobe"
        return self.ffmpeg

    def audio_streams(self, path):
        try:
            r = subprocess.run([self.tool("ffprobe"), "-v", "error", "-select_streams", "a", "-show_entries",
                                "stream=index", "-of", "csv=p=0", path], capture_output=True, text=True, timeout=30)
            return len([l for l in r.stdout.split() if l.strip()])
        except Exception:
            return 1

    def start_export(self):
        with self.lock:
            if self.export["state"] == "running":
                return False, "an export is already running"
            if not self.segments:
                return False, "the tape is empty"
            if self.state != "idle":
                return False, "an edit is being recorded"
            segs = [dict(s) for s in self.segments]
            self.export = {"state": "running", "progress": 0.0, "file": "", "error": ""}
        vi = obs.obs_video_info()
        size = (vi.output_width, vi.output_height) if obs.obs_get_video_info(vi) else (1920, 1080)
        threading.Thread(target=self.run_export, args=(segs, size), daemon=True).start()
        return True, "export started"

    def run_export(self, segs, size):
        try:
            missing = [s["file"] for s in segs if not os.path.exists(s["file"])]
            if missing:
                raise RuntimeError("recording missing: %s" % missing[0])
            streams = {}
            for s in segs:
                if s["file"] not in streams:
                    streams[s["file"]] = self.audio_streams(s["file"])
            ntr = min(4, max(streams.values() or [0]))
            start = min(s["in"] for s in segs)
            end = max(s["out"] for s in segs)
            w, h = size
            ext = "mov" if self.fmt == "prores" else "mp4"
            pix = "yuv422p10le" if self.fmt == "prores" else "yuv420p"
            folder = self.export_dir or self.dir
            os.makedirs(folder, exist_ok=True)
            name = os.path.join(folder, "BVE tape %s.%s" % (datetime.datetime.now().strftime("%Y-%m-%d %H-%M-%S"), ext))
            args = [self.ffmpeg, "-y", "-hide_banner", "-nostats", "-progress", "pipe:1"]
            graph, nin = [], [0]

            def add_input(seg, a, b):
                t0 = seg["offset"] + a / TC_FPS + seg.get("align", 0) / seg["fps"]
                args.extend(["-ss", "%.6f" % max(0.0, t0), "-t", "%.6f" % ((b - a) / TC_FPS + 1.0), "-i", seg["file"]])
                nin[0] += 1
                return nin[0] - 1

            vpieces = self.fill(self.resolve(segs, 0x04), start, end)
            apieces = [self.fill(self.resolve(segs, AUDIO_BITS if ntr == 1 else (0x08 << k)), start, end) for k in range(ntr)]
            labels = []
            for j, (a, b, seg) in enumerate(vpieces):
                n = b - a
                if seg is None:
                    graph.append("color=c=black:s=%dx%d:r=30000/1001,format=%s,trim=end_frame=%d,setsar=1[v%d]" % (w, h, pix, n, j))
                else:
                    i = add_input(seg, a, b)
                    graph.append("[%d:v:0]fps=30000/1001,scale=%d:%d:force_original_aspect_ratio=decrease,"
                                 "pad=%d:%d:(ow-iw)/2:(oh-ih)/2,setsar=1,format=%s,tpad=stop=-1:stop_mode=clone,"
                                 "trim=end_frame=%d,setpts=PTS-STARTPTS[v%d]" % (i, w, h, w, h, pix, n, j))
                labels.append("[v%d]" % j)
            graph.append("%sconcat=n=%d:v=1:a=0[vout]" % ("".join(labels), len(labels)))
            alabels = []
            for k, pieces in enumerate(apieces):
                labels = []
                for j, (a, b, seg) in enumerate(pieces):
                    ns = int(round((b - a) / TC_FPS * 48000))
                    src = None if seg is None or streams.get(seg["file"], 0) <= (0 if ntr == 1 else k) else seg
                    if src is None:
                        graph.append("anullsrc=r=48000:cl=stereo,atrim=end_sample=%d[a%d_%d]" % (ns, k, j))
                    else:
                        i = add_input(src, a, b)
                        graph.append("[%d:a:%d]aresample=48000,aformat=channel_layouts=stereo,apad,atrim=end_sample=%d,"
                                     "asetpts=PTS-STARTPTS[a%d_%d]" % (i, 0 if ntr == 1 else k, ns, k, j))
                    labels.append("[a%d_%d]" % (k, j))
                graph.append("%sconcat=n=%d:v=0:a=1[a%d]" % ("".join(labels), len(labels), k))
                alabels.append("[a%d]" % k)
            maps = ["-map", "[vout]"]
            if alabels and self.audio == "mix" and len(alabels) > 1:
                graph.append("%samix=inputs=%d:normalize=0[amix]" % ("".join(alabels), len(alabels)))
                maps += ["-map", "[amix]"]
            else:
                for k, l in enumerate(alabels):
                    maps += ["-map", l, "-metadata:s:a:%d" % k, "title=%s" % ("A1-A4" if ntr == 1 else "A%d" % (k + 1))]
            gfile = os.path.join(self.dir, "export.ffgraph")
            os.makedirs(self.dir, exist_ok=True)
            with open(gfile, "w") as f:
                f.write(";\n".join(graph))
            args += ["-filter_complex_script", gfile] + maps
            if self.fmt == "prores":
                args += ["-c:v", "prores_ks", "-profile:v", "3", "-vendor", "apl0", "-c:a", "pcm_s24le"]
            else:
                args += ["-c:v", "libx264", "-crf", "17", "-preset", "medium", "-c:a", "aac", "-b:a", "320k", "-movflags", "+faststart"]
            args += ["-timecode", tc_str(start), "-r", "30000/1001", name]
            total = (end - start) / TC_FPS
            link.msgs.append((obs.LOG_INFO, "tape: exporting %s - %s (%d segments) to %s" % (tc_str(start), tc_str(end), len(segs), name)))
            p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            err = []
            threading.Thread(target=lambda: err.extend(p.stderr.readlines()), daemon=True).start()
            for line in p.stdout:
                if line.startswith("out_time_us=") and line[12:].strip().isdigit():
                    with self.lock:
                        self.export["progress"] = min(1.0, int(line[12:]) / 1e6 / total) if total > 0 else 0.0
            p.wait()
            if p.returncode != 0:
                raise RuntimeError("ffmpeg failed: %s" % "".join(err[-6:]).strip()[-600:])
            with self.lock:
                self.export = {"state": "done", "progress": 1.0, "file": name, "error": ""}
            link.msgs.append((obs.LOG_INFO, "tape: exported %s" % name))
        except Exception as e:
            with self.lock:
                self.export = {"state": "failed", "progress": 0.0, "file": "", "error": str(e)}
            link.msgs.append((obs.LOG_WARNING, "tape: export failed: %s" % e))

    # ---------------------------------------------------------------- control server for the toolbar
    def serve(self):
        self.unserve()
        tape = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def allowed(self):
                # only pages on this machine (the emulator's UI) may drive it: Origin and Host must be loopback
                host = (self.headers.get("Host") or "").rsplit(":", 1)[0].strip("[]")
                if host not in ("127.0.0.1", "localhost", "::1"):
                    return False
                o = self.headers.get("Origin")
                if not o:
                    return True
                try:
                    return urllib.parse.urlparse(o).hostname in ("127.0.0.1", "localhost", "::1")
                except Exception:
                    return False

            def reply(self, code, obj=None):
                b = json.dumps(obj if obj is not None else {}).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(b)))
                self.send_header("Cache-Control", "no-store")
                o = self.headers.get("Origin")
                if o and code != 403:
                    self.send_header("Access-Control-Allow-Origin", o)
                    self.send_header("Vary", "Origin")
                self.end_headers()
                self.wfile.write(b)

            def do_OPTIONS(self):
                if not self.allowed():
                    return self.reply(403)
                self.send_response(204)
                self.send_header("Access-Control-Allow-Origin", self.headers.get("Origin") or "*")
                self.send_header("Access-Control-Allow-Methods", "GET, POST")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")
                self.end_headers()

            def do_GET(self):
                if not self.allowed():
                    return self.reply(403)
                if self.path.split("?")[0] == "/tape":
                    return self.reply(200, tape.status())
                self.reply(404, {"error": "not found"})

            def do_POST(self):
                if not self.allowed():
                    return self.reply(403)
                n = int(self.headers.get("Content-Length") or 0)
                body = self.rfile.read(n).decode(errors="replace").strip() if n else ""
                path = self.path.split("?")[0]
                msg = ""
                if path == "/tape/arm":
                    tape.set_armed(body != "off")
                elif path == "/tape/undo":
                    tape.undo()
                elif path == "/tape/clear":
                    tape.clear()
                elif path == "/tape/export":
                    ok, msg = tape.start_export()
                    if not ok:
                        st = tape.status()
                        st["message"] = msg
                        return self.reply(409, st)
                else:
                    return self.reply(404, {"error": "not found"})
                st = tape.status()
                st["message"] = msg
                self.reply(200, st)

        try:
            srv = http.server.ThreadingHTTPServer(("127.0.0.1", self.http_port), Handler)
        except OSError as e:
            link.msgs.append((obs.LOG_WARNING, "tape: cannot open the toolbar port %d (%s)" % (self.http_port, e)))
            return
        srv.daemon_threads = True
        self.server = srv
        threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.25}, daemon=True).start()

    def unserve(self):
        if self.server is not None:
            self.server.shutdown()
            self.server.server_close()
            self.server = None


link = Link()
ui = UiCalls()
tape = Tape()


def tc_frames(tc):
    try:
        h, m, s, f = [int(x) for x in tc.replace(";", ":").replace(".", ":").split(":")]
        return ((h * 60 + m) * 60 + s) * 30 + f
    except Exception:
        return 30 * 3600


# ---------------------------------------------------------------------- OBS script interface
def script_description():
    return ("<b>BVE-9100 link</b><br>Media sources as the editor's tape decks, the editor's switcher driving OBS's "
            "program (cuts, dissolves, wipes), and OBS recording each edit onto a record tape that exports as one "
            "file. Start the emulator first (npm start, port 8090); the OBS TAPE toolbar appears on its page.")


def script_defaults(s):
    obs.obs_data_set_default_string(s, "url", "http://127.0.0.1:8090")
    obs.obs_data_set_default_bool(s, "enabled", True)
    obs.obs_data_set_default_string(s, "sw_port", "E")
    obs.obs_data_set_default_bool(s, "follow_switcher", True)
    obs.obs_data_set_default_bool(s, "preview_follows_pst", True)
    obs.obs_data_set_default_bool(s, "obs_drives_switcher", False)
    obs.obs_data_set_default_bool(s, "verbose", False)
    obs.obs_data_set_default_bool(s, "audio_follow", True)
    for p in PORTS:
        obs.obs_data_set_default_string(s, "deck_%s_tc" % p, "01:00:00:00")
    obs.obs_data_set_default_string(s, "tape_rec_port", "A")
    obs.obs_data_set_default_string(s, "tape_dir", os.path.join(os.path.expanduser("~"), "BVE-9100 tape"))
    obs.obs_data_set_default_string(s, "tape_ffmpeg", "ffmpeg")
    obs.obs_data_set_default_string(s, "tape_fmt", "prores")
    obs.obs_data_set_default_string(s, "tape_audio", "tracks")
    obs.obs_data_set_default_int(s, "tape_align", TAPE_ALIGN)
    obs.obs_data_set_default_int(s, "program_delay", PROGRAM_DELAY)
    obs.obs_data_set_default_int(s, "tape_http", 8091)


def source_list(props, name, label, ids):
    p = obs.obs_properties_add_list(props, name, label, obs.OBS_COMBO_TYPE_EDITABLE, obs.OBS_COMBO_FORMAT_STRING)
    obs.obs_property_list_add_string(p, "", "")
    sources = obs.obs_enum_sources()
    if sources:
        for src in sources:
            if obs.obs_source_get_unversioned_id(src) in ids:
                n = obs.obs_source_get_name(src)
                obs.obs_property_list_add_string(p, n, n)
        obs.source_list_release(sources)
    return p


def script_properties():
    props = obs.obs_properties_create()
    obs.obs_properties_add_text(props, "url", "Emulator URL", obs.OBS_TEXT_DEFAULT)
    obs.obs_properties_add_bool(props, "enabled", "Link active")

    decks = obs.obs_properties_create()
    for p in PORTS:
        role = DEFAULT_ROLES[p]
        source_list(decks, "deck_%s_source" % p, "Port %s (%s) media source" % (p, role), ("ffmpeg_source", "vlc_source"))
        obs.obs_properties_add_text(decks, "deck_%s_tc" % p, "Port %s start timecode" % p, obs.OBS_TEXT_DEFAULT)
        obs.obs_properties_add_path(decks, "deck_%s_file" % p, "Port %s new media file (for Create deck scenes)" % p,
                                    obs.OBS_PATH_FILE, "Media (*.mp4 *.mov *.mkv *.avi *.mxf *.webm *.ts *.mpg);;All files (*.*)", None)
        source_list(decks, "deck_%s_text" % p, "Port %s timecode text source" % p, ("text_ft2_source", "text_gdiplus"))
    obs.obs_properties_add_group(props, "decks", "Tape decks (RS-422 ports with a VTR)", obs.OBS_GROUP_NORMAL, decks)

    sw = obs.obs_properties_create()
    obs.obs_properties_add_text(sw, "sw_port", "Switcher port", obs.OBS_TEXT_DEFAULT)
    obs.obs_properties_add_bool(sw, "follow_switcher", "Program follows the switcher")
    obs.obs_properties_add_bool(sw, "preview_follows_pst", "Studio mode preview follows the preset bus")
    obs.obs_properties_add_bool(sw, "obs_drives_switcher", "Scene changes in OBS go to the switcher")
    obs.obs_properties_add_int(sw, "program_delay", "Program delay (frames): takes follow the media's picture", 0, 15, 1)
    obs.obs_properties_add_bool(sw, "audio_follow", "Deck audio follows the editor (mutes the decks not heard)")
    scenes = obs.obs_frontend_get_scene_names() or []
    for x in range(1, NXPT + 1):
        p = obs.obs_properties_add_list(sw, "xpt_%d" % x, "Crosspoint %d scene" % x, obs.OBS_COMBO_TYPE_EDITABLE,
                                        obs.OBS_COMBO_FORMAT_STRING)
        obs.obs_property_list_add_string(p, "", "")
        for n in scenes:
            obs.obs_property_list_add_string(p, n, n)
    obs.obs_properties_add_group(props, "sw", "Switcher", obs.OBS_GROUP_NORMAL, sw)

    t = obs.obs_properties_create()
    obs.obs_properties_add_text(t, "tape_rec_port", "Record deck port", obs.OBS_TEXT_DEFAULT)
    obs.obs_properties_add_path(t, "tape_dir", "Tape folder (segment list, export)", obs.OBS_PATH_DIRECTORY, None, None)
    obs.obs_properties_add_path(t, "tape_export_dir", "Export folder (empty: the tape folder)", obs.OBS_PATH_DIRECTORY, None, None)
    obs.obs_properties_add_text(t, "tape_ffmpeg", "ffmpeg", obs.OBS_TEXT_DEFAULT)
    f = obs.obs_properties_add_list(t, "tape_fmt", "Export format", obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
    obs.obs_property_list_add_string(f, "ProRes 422 HQ, PCM (.mov)", "prores")
    obs.obs_property_list_add_string(f, "H.264, AAC (.mp4)", "h264")
    a = obs.obs_properties_add_list(t, "tape_audio", "Export audio", obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
    obs.obs_property_list_add_string(a, "A1-A4 as separate tracks", "tracks")
    obs.obs_property_list_add_string(a, "A1-A4 mixed to stereo", "mix")
    obs.obs_properties_add_int(t, "tape_align", "Line-up correction (frames)", -30, 30, 1)
    obs.obs_properties_add_int(t, "tape_http", "Toolbar port", 1024, 65535, 1)
    obs.obs_properties_add_button(t, "tape_export", "Export tape", lambda p, q: (tape.start_export(), False)[1])
    obs.obs_properties_add_button(t, "tape_undo", "Remove last segment", lambda p, q: (tape.undo(), False)[1])
    obs.obs_properties_add_button(t, "tape_clear", "Clear tape", lambda p, q: (tape.clear(), False)[1])
    obs.obs_properties_add_group(props, "tape", "Record tape (OBS records each edit; toolbar in the emulator's page)",
                                 obs.OBS_GROUP_NORMAL, t)

    obs.obs_properties_add_button(props, "create", "Create deck scenes", create_scenes)
    obs.obs_properties_add_bool(props, "verbose", "Log activity (script log)")
    return props


_settings = None


def script_update(s):
    global _settings
    _settings = s
    link.url = obs.obs_data_get_string(s, "url") or "http://127.0.0.1:8090"
    link.enabled = obs.obs_data_get_bool(s, "enabled")
    link.decks = {}
    for p in PORTS:
        cfg = {"source": obs.obs_data_get_string(s, "deck_%s_source" % p),
               "start": tc_frames(obs.obs_data_get_string(s, "deck_%s_tc" % p) or "01:00:00:00"),
               "text": obs.obs_data_get_string(s, "deck_%s_text" % p),
               "file": obs.obs_data_get_string(s, "deck_%s_file" % p),
               "role": DEFAULT_ROLES[p]}
        if cfg["source"] or cfg["text"]:
            link.decks[p] = cfg
    link.sw_port = (obs.obs_data_get_string(s, "sw_port") or "E").strip().upper()[:1]
    link.follow_switcher = obs.obs_data_get_bool(s, "follow_switcher")
    link.preview_follows_pst = obs.obs_data_get_bool(s, "preview_follows_pst")
    link.obs_drives_switcher = obs.obs_data_get_bool(s, "obs_drives_switcher")
    link.verbose = obs.obs_data_get_bool(s, "verbose")
    link.delay = max(0, obs.obs_data_get_int(s, "program_delay"))
    tape.rec_port = (obs.obs_data_get_string(s, "tape_rec_port") or "A").strip().upper()[:1]
    tape.dir = obs.obs_data_get_string(s, "tape_dir") or os.path.join(os.path.expanduser("~"), "BVE-9100 tape")
    tape.export_dir = obs.obs_data_get_string(s, "tape_export_dir")
    tape.ffmpeg = obs.obs_data_get_string(s, "tape_ffmpeg") or "ffmpeg"
    tape.fmt = obs.obs_data_get_string(s, "tape_fmt") or "prores"
    tape.audio = obs.obs_data_get_string(s, "tape_audio") or "tracks"
    tape.align = link.delay + obs.obs_data_get_int(s, "tape_align")
    port = obs.obs_data_get_int(s, "tape_http") or 8091
    tape.load()
    if port != tape.http_port or tape.server is None:
        tape.http_port = port
        if link.thread is not None:              # after script_load: reopen on the new port
            tape.serve()
    link.xpt = {}
    for x in range(1, NXPT + 1):
        n = obs.obs_data_get_string(s, "xpt_%d" % x)
        if n:
            link.xpt[x] = n
    link.last_take = None
    link.last_pst = None
    link.audio_follow = obs.obs_data_get_bool(s, "audio_follow")
    link.scene_decks = {}
    link.muted = {}


def deck_media_settings(path=None):
    """Media source settings a deck needs: no looping, no restart when shown, frame kept at the end."""
    ms = obs.obs_data_create()
    if path:
        obs.obs_data_set_bool(ms, "is_local_file", True)
        obs.obs_data_set_string(ms, "local_file", path)
    obs.obs_data_set_bool(ms, "looping", False)
    obs.obs_data_set_bool(ms, "restart_on_activate", False)
    obs.obs_data_set_bool(ms, "close_when_inactive", False)
    obs.obs_data_set_bool(ms, "clear_on_media_end", False)
    return ms


def create_scenes(props, prop):
    """A scene 'BVE <role>' per deck, holding the deck's media source: a new 'BVE <role> deck' source when a file is
    chosen for the deck, otherwise the media source already picked for it. Crosspoints 1-4 are mapped to the scenes
    as the editor's default XPT ASSIGN (1 = R, 2 = P1, 3 = P2, 4 = P3). Also makes empty scenes for the editor's other
    sources, to fill with anything (a camera, graphics, ...): 'BVE AX1', 'BVE AX2' and 'BVE BLACK' on crosspoints 8, 9
    and 10 (what the emulator assigns them; an empty scene is black)."""
    if _settings is None:
        return False
    made, decks = 0, []
    for i, p in enumerate(PORTS):
        role = DEFAULT_ROLES[p]
        path = obs.obs_data_get_string(_settings, "deck_%s_file" % p)
        src_name = obs.obs_data_get_string(_settings, "deck_%s_source" % p)
        if path:
            src_name = "BVE %s deck" % role
        elif not src_name:
            continue
        src = obs.obs_get_source_by_name(src_name)
        ms = deck_media_settings(path)
        if src is None:
            if not path:
                obs.obs_data_release(ms)
                link.log("port %s: no source named %s" % (p, src_name), obs.LOG_WARNING)
                continue
            src = obs.obs_source_create("ffmpeg_source", src_name, ms, None)
        elif obs.obs_source_get_unversioned_id(src) == "ffmpeg_source":
            obs.obs_source_update(src, ms)
        obs.obs_data_release(ms)
        scene_name = "BVE %s" % role
        sc = obs.obs_get_source_by_name(scene_name)
        if sc is None:
            scene = obs.obs_scene_create(scene_name)
            fit_to_screen(obs.obs_scene_add(scene, src))
            obs.obs_scene_release(scene)
        else:
            scene = obs.obs_scene_from_source(sc)
            if obs.obs_scene_find_source(scene, src_name) is None:
                fit_to_screen(obs.obs_scene_add(scene, src))
            obs.obs_source_release(sc)
        obs.obs_source_release(src)
        obs.obs_data_set_string(_settings, "deck_%s_source" % p, src_name)
        obs.obs_data_set_string(_settings, "xpt_%d" % (i + 1), scene_name)
        made += 1
        decks.append((scene_name, src_name))
    if decks:
        audio_scene(decks)
    extra = []
    for xpt, scene_name in EXTRA_SCENES:
        sc = obs.obs_get_source_by_name(scene_name)
        if sc is None:
            obs.obs_scene_release(obs.obs_scene_create(scene_name))
            extra.append(scene_name)
        else:
            obs.obs_source_release(sc)
        if not obs.obs_data_get_string(_settings, "xpt_%d" % xpt):
            obs.obs_data_set_string(_settings, "xpt_%d" % xpt, scene_name)
    if extra:
        link.log("created %s (empty: put anything in them)" % ", ".join(extra))
    if made == 0:
        link.log("Create deck scenes: pick a media source or a file for at least one deck first", obs.LOG_WARNING)
    else:
        link.log("created / updated %d deck scenes" % made)
    script_update(_settings)
    return True


AUDIO_SCENE = "BVE audio"
EXTRA_SCENES = ((8, "BVE AX1"), (9, "BVE AX2"), (10, "BVE BLACK"))   # crosspoint, scene


def audio_scene(decks):
    """OBS only mixes the audio of sources on program, but an audio-only edit takes its sound from a deck that is not on
    screen. So every deck's media source also goes into 'BVE audio', off the canvas (live, but not seen), which is
    nested in each deck scene; the script then mutes and unmutes the decks to follow the editor (Link.route_audio)."""
    vi = obs.obs_video_info()
    w = vi.base_width if obs.obs_get_video_info(vi) else 1920
    sc = obs.obs_get_source_by_name(AUDIO_SCENE)
    scene = obs.obs_scene_from_source(sc) if sc is not None else obs.obs_scene_create(AUDIO_SCENE)
    for k, (_, src_name) in enumerate(decks):
        if obs.obs_scene_find_source(scene, src_name) is None:
            src = obs.obs_get_source_by_name(src_name)
            if src is not None:
                item = obs.obs_scene_add(scene, src)
                pos = obs.vec2()
                pos.x, pos.y = w * (2 + k), 0                # off the canvas
                obs.obs_sceneitem_set_pos(item, pos)
                obs.obs_source_release(src)
    asrc = obs.obs_scene_get_source(scene)
    for scene_name, _ in decks:
        dsc = obs.obs_get_source_by_name(scene_name)
        if dsc is None:
            continue
        dscene = obs.obs_scene_from_source(dsc)
        if obs.obs_scene_find_source(dscene, AUDIO_SCENE) is None:
            item = obs.obs_scene_add(dscene, asrc)
            obs.obs_sceneitem_set_order(item, obs.OBS_ORDER_MOVE_BOTTOM)
        obs.obs_source_release(dsc)
    if sc is not None:
        obs.obs_source_release(sc)
    else:
        obs.obs_scene_release(scene)


def fit_to_screen(item):
    if item is None:
        return
    vi = obs.obs_video_info()
    if not obs.obs_get_video_info(vi):
        return
    obs.obs_sceneitem_set_bounds_type(item, obs.OBS_BOUNDS_SCALE_INNER)
    b = obs.vec2()
    b.x, b.y = vi.base_width, vi.base_height
    obs.obs_sceneitem_set_bounds(item, b)


# hotkeys: transports for each deck, CUT / AUTO for the switcher
HOTKEYS = [("deck_%s_%s" % (p, c), "BVE-9100 %s (port %s) %s" % (DEFAULT_ROLES[p], p, c.upper()), p, c)
           for p in "ABCD" for c in ("play", "still", "stop", "ff", "rew")]
HOTKEYS += [("sw_cut", "BVE-9100 switcher CUT", None, "cut"), ("sw_auto", "BVE-9100 switcher AUTO", None, "auto")]


def hotkey_cb(port, cmd):
    def cb(pressed):
        if pressed:
            link.post(port or link.sw_port, cmd)
    return cb


def script_load(s):
    for name, desc, port, cmd in HOTKEYS:
        hid = obs.obs_hotkey_register_frontend("bve9100_" + name, desc, hotkey_cb(port, cmd))
        arr = obs.obs_data_get_array(s, "hk_" + name)
        obs.obs_hotkey_load(hid, arr)
        obs.obs_data_array_release(arr)
        link.hotkeys[name] = hid
    script_update(s)
    if ui.lib is None:
        link.log("ctypes cannot reach obs-frontend-api: scene switches go through obspython (may stall OBS if other scripts use frontend callbacks)", obs.LOG_WARNING)
    link.start()
    tape.serve()
    obs.timer_add(link.tick, 33)


def script_save(s):
    for name, hid in link.hotkeys.items():
        arr = obs.obs_hotkey_save(hid)
        obs.obs_data_set_array(s, "hk_" + name, arr)
        obs.obs_data_array_release(arr)


def script_unload():
    obs.timer_remove(link.tick)
    link.stop.set()
    tape.unserve()
