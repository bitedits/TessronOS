#!/usr/bin/env python3
"""How loud given tones are, left and right, through a WAVE file.

  tools/tests/wav_tones.py FILE [HZ ...]	(default 500 1500)

For every half second of FILE, the strength of each tone (Goertzel) in
the left and the right channel, as a line: what tools/tests/ui_sndmix.steps
did to two programs' tones can be read off it (one turned down, one
muted, the whole's left turned down).
"""

import math
import struct
import sys
import wave


def strength(x, rate, hz):
    k = 2.0 * math.cos(2.0 * math.pi * hz / rate)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + k * s1 - s2
        s2, s1 = s1, s0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return math.sqrt(max(p, 0.0)) / max(len(x), 1) * 2.0


def main():
    w = wave.open(sys.argv[1])
    tones = [float(a) for a in sys.argv[2:]] or [500.0, 1500.0]
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    d = w.readframes(n)
    s = struct.unpack('<%dh' % (len(d) // 2), d)
    win = rate // 2
    print("%d Hz, %d channels, %.1f s" % (rate, ch, n / rate))
    print(" time  " + "  ".join("%6dHz L %6dHz R" % (t, t) for t in tones))
    for i in range(0, n - win + 1, win):
        l = s[i * ch:(i + win) * ch:ch]
        r = s[i * ch + 1:(i + win) * ch:ch] if ch > 1 else l
        cells = []
        for t in tones:
            cells.append("%9.0f %9.0f" % (strength(l, rate, t), strength(r, rate, t)))
        print("%5.1f  %s" % (i / rate, "  ".join(cells)))


if __name__ == "__main__":
    main()
