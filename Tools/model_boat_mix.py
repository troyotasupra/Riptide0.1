"""Offline model of the boat's sound mix, for checking nothing is too loud (Python standard library only).

Unreal's silent (non-realtime) audio renderer doesn't mix during play-in-editor, so the in-game output can't be
recorded without playing it out loud. Instead this rebuilds the mix the way the engine does, from the decoded
sounds:
  - each sound at its asset volume (from SOUNDS / PEAK_CEILING_DBFS in Content/Python/init_unreal.py),
  - times the runtime volume and pitch the boat sets (logged by boat_handling_test.py; pitch = resampling),
  - spatialized mono-summed sounds panned centre at equal power (0.707 per channel, as the helm hears them
    inside the 5 m full-volume radius), the ocean ambience left stereo and unspatialized,
then sums them and measures loudness and peak with measure_loudness.py.

  python Tools/model_boat_mix.py <folder of WAVs exported from the imported sounds>

The WAVs are named after the source files (engine_outboard.wav, hull_wash.wav, ocean_waves.wav, hull_slap_08.wav).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure_loudness as ml  # noqa: E402


SECONDS = 20
CENTRE_PAN = math.sqrt(0.5)

# Measured source levels and targets, as in init_unreal.py.
LEVELS = {  # file: (measured LUFS, measured peak dBFS, target LUFS)
    "engine_outboard": (-14.1, -9.8, -22.0),
    "hull_wash": (-22.1, -13.8, -24.0),
    "ocean_waves": (-9.3, 0.0, -28.0),
    "hull_slap_08": (-14.5, -0.5, -22.0),
}
PEAK_CEILING_DBFS = -8.0

# Runtime settings seen in the handling test (engine volume/pitch, wash volume/pitch), and slap strengths.
SCENARIOS = {
    "at rest, idling": {"engine": (0.40, 0.75), "wash": (0.0, 0.85), "slaps": []},
    "full throttle, typical slaps": {"engine": (1.0, 1.90), "wash": (0.97, 1.14), "slaps": [(4.0, 0.48), (11.0, 0.34), (16.0, 0.48)]},
    "full throttle, hardest slap": {"engine": (1.0, 1.90), "wash": (1.0, 1.15), "slaps": [(4.0, 1.0), (11.0, 1.0)]},
}


def asset_volume(name):
    lufs, peak, target = LEVELS[name]
    return 10 ** (min(target - lufs, PEAK_CEILING_DBFS - peak) / 20)


def load_mono(folder, name, rate_out):
    chans, rate = ml.read(os.path.join(folder, name + ".wav"))
    mono = [sum(s) / len(chans) for s in zip(*chans)]
    return mono, rate


def looped(mono, rate, pitch, n_out, rate_out):
    """Loop and resample a mono sound: pitch 2.0 plays it twice as fast, as Unreal's pitch multiplier does."""
    step = pitch * rate / rate_out
    out = [0.0] * n_out
    pos = 0.0
    length = len(mono)
    for i in range(n_out):
        j = int(pos)
        frac = pos - j
        a, b = mono[j % length], mono[(j + 1) % length]
        out[i] = a + (b - a) * frac
        pos += step
    return out


def main(folder):
    rate = 48000
    n = SECONDS * rate
    engine, engine_rate = load_mono(folder, "engine_outboard", rate)
    wash, wash_rate = load_mono(folder, "hull_wash", rate)
    slap, slap_rate = load_mono(folder, "hull_slap_08", rate)
    ocean_ch, ocean_rate = ml.read(os.path.join(folder, "ocean_waves.wav"))
    ocean = [looped(c, ocean_rate, 1.0, n, rate) for c in ocean_ch[:2]]
    if len(ocean) == 1:
        ocean = ocean * 2

    for title, s in SCENARIOS.items():
        ev, ep = s["engine"]
        wv, wp = s["wash"]
        eng = looped(engine, engine_rate, ep, n, rate)
        wsh = looped(wash, wash_rate, wp, n, rate)
        centre = [asset_volume("engine_outboard") * ev * a + asset_volume("hull_wash") * wv * b for a, b in zip(eng, wsh)]
        for at, strength in s["slaps"]:
            one = looped(slap, slap_rate, 1.0, int(len(slap) * rate / slap_rate), rate)
            start = int(at * rate)
            for i, v in enumerate(one):
                if start + i < n:
                    centre[start + i] += asset_volume("hull_slap_08") * strength * v
        amb = asset_volume("ocean_waves")
        mix = [[CENTRE_PAN * c + amb * o for c, o in zip(centre, ocean[ch])] for ch in range(2)]
        peak = max(max(abs(v) for v in ch) for ch in mix)
        integrated, loudest = ml.lufs(mix, rate)
        print("%-30s loudness %6.1f LUFS   loudest moment %6.1f LUFS   peak %6.1f dBFS" % (
            title, integrated, loudest, ml.db(peak)), flush=True)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
    else:
        main(sys.argv[1])
