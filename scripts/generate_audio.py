#!/usr/bin/env python3
"""Create original deterministic combat effects using only Python's standard library."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
import random
import struct
import wave

SAMPLE_RATE = 48_000
ROOT = Path(__file__).resolve().parents[1]
SOUNDS = {"Slash": 0.24, "Clash": 0.7, "Hit": 0.28, "Dodge": 0.32, "Execute": 1.05}


def synthesize(name: str, duration: float) -> list[float]:
    """Combine filtered noise, frequency sweeps, and damped metal resonances."""
    rng = random.Random(2718 + sum(ord(c) for c in name))
    frames = round(SAMPLE_RATE * duration)
    samples = []
    low_noise = 0.0
    phase = 0.0
    for index in range(frames):
        t = index / SAMPLE_RATE
        u = t / duration
        noise = rng.uniform(-1, 1)
        low_noise += 0.14 * (noise - low_noise)
        high_noise = noise - low_noise
        if name == "Slash":
            envelope = math.sin(math.pi * u) ** 1.6
            phase += math.tau * (1300 - 1000 * u) / SAMPLE_RATE
            sample = envelope * (0.8 * high_noise + 0.12 * math.sin(phase))
        elif name == "Clash":
            ring = sum(math.sin(math.tau * hz * t) * math.exp(-t * decay) * amplitude
                       for hz, decay, amplitude in ((1860, 8, 0.5), (2937, 11, 0.3), (4211, 15, 0.2)))
            sample = ring + 0.65 * high_noise * math.exp(-t * 75)
        elif name == "Hit":
            phase += math.tau * (55 + 180 * math.exp(-t * 45)) / SAMPLE_RATE
            sample = 0.65 * math.sin(phase) * math.exp(-t * 24) + 0.6 * low_noise * math.exp(-t * 35)
        elif name == "Dodge":
            envelope = math.sin(math.pi * u) ** 2.2
            sample = 1.8 * low_noise * envelope + 0.18 * high_noise * envelope
        elif name == "Execute":
            phase += math.tau * (42 + 100 * math.exp(-t * 14)) / SAMPLE_RATE
            bass = 0.55 * math.sin(phase) * math.exp(-t * 7)
            ring = 0.2 * math.sin(math.tau * 1723 * t) * math.exp(-t * 5)
            sample = bass + ring + 0.7 * noise * math.exp(-t * 36)
        else:
            raise ValueError(f"Unknown effect: {name}")
        # Short edges prevent sample discontinuities; retain headroom in every file.
        edge = min(1.0, t / 0.002, (duration - t) / 0.012)
        samples.append(sample * max(0.0, edge))
    samples[0] = samples[-1] = 0.0
    peak = max(abs(sample) for sample in samples) or 1.0
    return [sample * 0.82 / peak for sample in samples]


def generate(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for name, duration in SOUNDS.items():
        samples = synthesize(name, duration)
        pcm = struct.pack(f"<{len(samples)}h", *(round(sample * 32767) for sample in samples))
        destination = output / f"{name}.wav"
        with wave.open(str(destination), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(SAMPLE_RATE)
            wav.writeframes(pcm)
        print(f"{destination}: {duration:g}s, mono, {SAMPLE_RATE} Hz, 16-bit PCM")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".cache" / "audio")
    generate(parser.parse_args().output)
