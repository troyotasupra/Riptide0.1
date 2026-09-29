"""Loudness report for 16-bit WAV files, Python standard library only (works on Windows and macOS).

  python Tools/measure_loudness.py <file.wav or folder> [...] [--pitch]

Per file: peak (dBFS), RMS (dBFS), integrated loudness in LUFS per ITU-R BS.1770 (K-weighting, 400 ms
blocks, -70 LUFS absolute and -10 LU relative gates), the loudest 400 ms moment, how much the level swings,
and the jump at the loop seam (a click risk for loops). --pitch adds the dominant pitch, for picking engine
loops. Unreal can turn OGG/MP3 into WAV: import the sound, then Asset Actions > Export.
"""
import array
import glob
import math
import os
import sys
import wave

MAX_SECONDS = 90  # long files are measured on their first 90 s


def read(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        n = min(n, rate * MAX_SECONDS)
        raw = w.readframes(n)
    assert width == 2, "expected 16-bit"
    a = array.array("h", raw)
    if sys.byteorder == "big":
        a.byteswap()
    chans = [[a[i] / 32768.0 for i in range(c, len(a), ch)] for c in range(ch)]
    return chans, rate


def biquad(x, b0, b1, b2, a1, a2):
    y = [0.0] * len(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def k_weight(x, rate):
    # BS.1770 pre-filter (high shelf) and RLB high-pass, coefficients derived for any sample rate.
    f0, g, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = math.tan(math.pi * f0 / rate)
    vh, vb = 10 ** (g / 20), 10 ** (g / 20) ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    x = biquad(x, (vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
               2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0)
    f0, q = 38.13547087602444, 0.5003270373238773
    k = math.tan(math.pi * f0 / rate)
    a0 = 1 + k / q + k * k
    return biquad(x, 1.0, -2.0, 1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0)


def lufs(chans, rate):
    """Returns (integrated LUFS, loudest 400 ms block in LUFS)."""
    weighted = [k_weight(c, rate) for c in chans]
    block, hop = int(0.4 * rate), int(0.1 * rate)
    n = len(weighted[0])
    if n < block:  # short one-shots: one block over the whole sound
        block, hop = n, n
    powers = []
    for s in range(0, n - block + 1, hop):
        p = sum(sum(v * v for v in w[s:s + block]) / block for w in weighted)
        powers.append(p)
    gated = [p for p in powers if p > 0 and -0.691 + 10 * math.log10(p) > -70]
    if not gated:
        return -99.0, -99.0
    loudest = -0.691 + 10 * math.log10(max(gated))
    rel = -0.691 + 10 * math.log10(sum(gated) / len(gated)) - 10
    gated = [p for p in gated if -0.691 + 10 * math.log10(p) > rel]
    return -0.691 + 10 * math.log10(sum(gated) / len(gated)), loudest


def db(v):
    return 20 * math.log10(v) if v > 0 else -99.0


def pitch(mono, rate):
    # Autocorrelation of a 1 s excerpt, searching 30-400 Hz, on a 4x-decimated signal.
    step = 4
    x = mono[: rate][::step]
    r = rate / step
    mean = sum(x) / len(x)
    x = [v - mean for v in x]
    best, lag_best = 0.0, 0
    for lag in range(int(r / 400), int(r / 30)):
        c = sum(x[i] * x[i + lag] for i in range(0, len(x) - lag, 2))
        if c > best:
            best, lag_best = c, lag
    energy = sum(v * v for v in x[::2]) or 1.0
    return (r / lag_best if lag_best else 0.0), best / energy


def steadiness(mono, rate):
    w = int(0.1 * rate)
    levels = [db(math.sqrt(sum(v * v for v in mono[s:s + w]) / w)) for s in range(0, len(mono) - w, w)]
    return (max(levels) - min(levels)) if levels else 0.0


def main(args):
    want_pitch = "--pitch" in args
    paths = []
    for a in (a for a in args if not a.startswith("--")):
        paths += sorted(glob.glob(os.path.join(a, "*.wav"))) if os.path.isdir(a) else [a]
    if not paths:
        print(__doc__)
        return
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        chans, rate = read(path)
        mono = [sum(s) / len(chans) for s in zip(*chans)]
        peak = max(max(abs(v) for v in c) for c in chans)
        rms = math.sqrt(sum(v * v for v in mono) / len(mono))
        integrated, loudest = lufs(chans, rate)
        row = "%-16s %dch %5.1fs  peak %6.1f dBFS  rms %6.1f dBFS  loudness %6.1f LUFS  loudest moment %6.1f LUFS" % (
            name, len(chans), len(mono) / rate, db(peak), db(rms), integrated, loudest)
        row += "  swing %4.1f dB  seam %.3f" % (steadiness(mono, rate), abs(mono[-1] - mono[0]))
        if want_pitch:
            f, strength = pitch(mono, rate)
            row += "  pitch %5.0f Hz (periodicity %.2f)" % (f, strength)
        print(row, flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
