"""Example algorithms only: no IPC, files, CLAP, device access or AI dependency.

Replace this class with your model/API adapter. Its constructor can load a model
once; begin() can schedule text generation; process() consumes/produces PCM.
This fixture generates a TEST TONE, not speech or a voice-conversion model.
"""
import math


class Processor:
    def __init__(self):
        self.phase = 0
        self.remaining = 0

    def reset(self):
        self.phase = 0
        self.remaining = 0

    def begin(self, frames):
        if type(frames) is not int or not 1 <= frames <= 48_000 * 10:
            raise ValueError("generation length must be 1..480000 frames")
        self.reset()
        self.remaining = frames

    def process(self, mode, incoming, outgoing, frames, gain=1.0):
        """Views are planar: L starts at 0, R at 480. Never retain a view.

        A real external model can block here: this is its OWN process, never the
        audio callback. The future RT adapter must enforce deadlines/fallback.
        'done' is explicit; short generation is zero-padded to the requested size.
        """
        if mode not in ("effect", "source"):
            raise ValueError("mode must be effect or source")
        if type(gain) not in (int, float) or not math.isfinite(gain) or not 0 <= gain <= 4:
            raise ValueError("gain must be finite in 0..4")
        produced = frames if mode == "effect" else min(frames, self.remaining)
        for i in range(frames):
            if mode == "effect":
                outgoing[i] = incoming[i] * gain
                outgoing[480 + i] = incoming[480 + i] * gain
            else:
                value = 0.05 * math.sin(2 * math.pi * 440 * self.phase / 48_000) if i < produced else 0
                outgoing[i] = outgoing[480 + i] = value
                if i < produced:
                    self.phase += 1
        if mode == "source":
            self.remaining -= produced
        return produced, mode == "source" and self.remaining == 0
