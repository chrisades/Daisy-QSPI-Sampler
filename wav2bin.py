#!/usr/bin/env python3
"""
wav2bin.py - pack a folder of .wav files into ONE mono int16 sample-bank .bin
for the Daisy Seed's QSPI flash.
 
Every .wav found is decoded (any bit depth/format, channel count, or rate),
mixed to mono, resampled, faded, normalized, and appended to the bank as
16-bit samples. Files are sorted naturally by name ("kick2" before "kick10"),
and that order is the clip index.
 
Layout (little-endian):
    header:       uint32 numClips, uint32 sampleRate
    index table:  numClips x { uint32 start, uint32 end, char name[24] }
    sample data:  all clips back to back, int16
 
start/end are sample indices (end exclusive) into the data region, which
begins at byte offset 8 + 32 * numClips. Each entry's name is the clip's
filename, NUL-padded/truncated to 24 bytes, so firmware can look clips up by
name (SamplePlayer::Replay("kick.wav")). Names must be unique and under 24 chars. 
Collisions or truncation just print a warning.The 24-byte width must match 
SamplePlayer::NAME_FIELD_LEN in SamplePlayer.h.
 
Examples:
    python wav2bin.py samples/
    python wav2bin.py samples/ --rate 96000
    python wav2bin.py kick.wav kick.bin
"""

import argparse
import math
import os
import re
import struct
import sys
from array import array

HEADER_FMT = "<II"
# Bytes reserved for each clip's name in the index table, including the NUL
# terminator. Must match SamplePlayer::NAME_FIELD_LEN in SamplePlayer.h exactly.
NAME_FIELD_LEN = 24
ENTRY_FMT = "<II%ds" % NAME_FIELD_LEN
QSPI_END = 0x90800000   # end of the Seed's 8 MB QSPI flash
APP_START = 0x90040000  # Daisy bootloader reserves 0x90000000-0x9003FFFF
PEAK_TARGET = 0.989     # -0.1 dBFS


class WavError(Exception):
    pass


def parse_wav(path):
    """Return (format_tag, channels, sample_rate, bits, raw_data_bytes)."""
    with open(path, "rb") as f:
        blob = f.read()
    if len(blob) < 12 or blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise WavError("not a RIFF/WAVE file")

    fmt, pcm, pos = None, None, 12
    while pos + 8 <= len(blob):
        cid = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        start = pos + 8
        end = min(start + size, len(blob))  # tolerate bogus/streamed sizes
        if cid == b"fmt ":
            tag, ch, sr, _, _, bits = struct.unpack_from("<HHIIHH", blob, start)
            if tag == 0xFFFE and size >= 26:  # extensible: real tag is in the GUID
                tag = struct.unpack_from("<H", blob, start + 24)[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b"data":
            pcm = blob[start:end]
        pos = start + size + (size & 1)  # chunks are word-aligned

    if fmt is None:
        raise WavError("no 'fmt ' chunk found")
    if pcm is None:
        raise WavError("no 'data' chunk found")
    return fmt + (pcm,)


def decode(pcm, tag, bits):
    """Decode raw bytes to a flat list of floats in [-1, 1]."""
    if tag == 1:  # integer PCM
        if bits == 8:
            return [(b - 128) / 128.0 for b in pcm]
        if bits == 16:
            n = len(pcm) // 2
            return [v / 32768.0 for v in struct.unpack("<%dh" % n, pcm[:n * 2])]
        if bits == 24:
            n = len(pcm) // 3
            return [int.from_bytes(pcm[i * 3:i * 3 + 3], "little", signed=True) / 8388608.0
                    for i in range(n)]
        if bits == 32:
            n = len(pcm) // 4
            return [v / 2147483648.0 for v in struct.unpack("<%di" % n, pcm[:n * 4])]
    elif tag == 3:  # IEEE float
        if bits == 32:
            n = len(pcm) // 4
            return list(struct.unpack("<%df" % n, pcm[:n * 4]))
        if bits == 64:
            n = len(pcm) // 8
            return list(struct.unpack("<%dd" % n, pcm[:n * 8]))
    raise WavError("unsupported WAV encoding (format tag %d, %d-bit)" % (tag, bits))


def resample(x, sr_in, sr_out):
    """Linear-interpolation resampler."""
    if sr_in == sr_out or len(x) < 2:
        return x
    ratio = sr_in / sr_out
    n_out = int(len(x) * sr_out / sr_in)
    last = len(x) - 1
    out = []
    for i in range(n_out):
        pos = i * ratio
        j = int(pos)
        if j >= last:
            out.append(x[last])
        else:
            frac = pos - j
            out.append(x[j] + (x[j + 1] - x[j]) * frac)
    return out


def apply_fades(x, sr, fade_in_ms, fade_out_ms):
    """Raised-cosine fade in/out, in place. First and last sample become exactly 0."""
    n_in = min(int(sr * fade_in_ms / 1000.0), len(x) // 2)
    n_out = min(int(sr * fade_out_ms / 1000.0), len(x) // 2)
    for i in range(n_in):
        x[i] *= 0.5 * (1.0 - math.cos(math.pi * i / n_in))
    base = len(x) - n_out
    for k in range(n_out):
        x[base + k] *= 0.5 * (1.0 + math.cos(math.pi * (k + 1) / n_out))
    return x


def encode_int16(samples):
    a = array("h", [max(-32768, min(32767, int(round(v * 32768.0)))) for v in samples])
    if sys.byteorder == "big":
        a.byteswap()
    return a.tobytes()


def pack_name(name, index, seen):
    """Encode a clip name to a fixed-width, NUL-terminated/padded field.

    Warns (but still packs) if the name doesn't fit or collides with an
    earlier clip, since SamplePlayer looks clips up by exact name match.
    """
    raw = name.encode("ascii", "replace")
    max_len = NAME_FIELD_LEN - 1  # leave room for the terminator
    if len(raw) > max_len:
        print("warning: clip name %r is longer than %d chars and will be "
              "truncated to %r" % (name, max_len, raw[:max_len].decode("ascii")),
              file=sys.stderr)
        raw = raw[:max_len]
    if raw in seen:
        print("warning: clip %d's name %r duplicates clip %d's; "
              "lookup by name will only find clip %d"
              % (index, raw.decode("ascii"), seen[raw], seen[raw]),
              file=sys.stderr)
    else:
        seen[raw] = index
    return raw + b"\x00" * (NAME_FIELD_LEN - len(raw))


def natural_key(path):
    """Sort key so that kick2.wav comes before kick10.wav."""
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", path)]


def find_wavs(root, recursive):
    if os.path.isfile(root):
        return [root]
    if not os.path.isdir(root):
        sys.exit("error: %s is not a file or folder" % root)
    found = []
    if recursive:
        for d, _, files in os.walk(root):
            found += [os.path.join(d, f) for f in files if f.lower().endswith(".wav")]
    else:
        for f in os.listdir(root):
            full = os.path.join(root, f)
            if f.lower().endswith(".wav") and os.path.isfile(full):
                found.append(full)
    return sorted(found, key=natural_key)


def load_clip(path, rate, fade_in_ms, fade_out_ms):
    """Decode one WAV to a mono float list at the target rate, with fades applied."""
    tag, nch, sr_in, bits, pcm = parse_wav(path)
    if nch < 1:
        raise WavError("WAV reports zero channels")
    samples = decode(pcm, tag, bits)
    frames = len(samples) // nch
    if frames == 0:
        raise WavError("WAV contains no audio data")

    # mono mixdown: average all channels (a mono file passes through untouched)
    if nch == 1:
        mono = samples[:frames]
    else:
        chans = [samples[c:frames * nch:nch] for c in range(nch)]
        mono = [sum(t) / nch for t in zip(*chans)]

    if sr_in > rate:
        print("%s: downsampling %d -> %d Hz with no anti-alias filter"
              % (path, sr_in, rate), file=sys.stderr)
    mono = resample(mono, sr_in, rate)
    if not mono:
        raise WavError("audio is too short to resample")
    return apply_fades(mono, rate, fade_in_ms, fade_out_ms)


def main():
    p = argparse.ArgumentParser(description="Pack a folder of WAVs into one mono int16 Daisy QSPI .bin")
    p.add_argument("input", help="folder containing .wav files (or a single .wav)")
    p.add_argument("output", nargs="?",
                   help="output .bin (default: <folder name>.bin in the current directory)")
    p.add_argument("-r", "--rate", type=int, default=48000, help="target sample rate (default 48000)")
    p.add_argument("--normalize", choices=("clip", "bank", "none"), default="clip",
                   help="clip: each clip to -0.1 dBFS (default); bank: one shared gain; none")
    p.add_argument("--fade-in", type=float, default=1.0, metavar="MS",
                   help="fade-in length in ms (default 1.0, 0 = off)")
    p.add_argument("--fade-out", type=float, default=1.0, metavar="MS",
                   help="fade-out length in ms (default 1.0, 0 = off)")
    p.add_argument("--recursive", action="store_true", help="also search subfolders")
    p.add_argument("--address", default="0x90040000", help="intended address location (default 0x90040000)")
    args = p.parse_args()

    if args.output:
        out_path = args.output
    elif os.path.isdir(args.input):
        out_path = os.path.basename(os.path.abspath(args.input)) + ".bin"
    else:
        out_path = os.path.splitext(args.input)[0] + ".bin"

    files = find_wavs(args.input, args.recursive)
    if not files:
        sys.exit("error: no .wav files found in %s" % args.input)

    # decode every clip (a bad file aborts: skipping it would silently shift the indices)
    clips = []
    for path in files:
        try:
            clips.append(load_clip(path, args.rate, args.fade_in, args.fade_out))
        except WavError as e:
            sys.exit("error: %s: %s" % (path, e))

    # levels
    peaks = [max(abs(v) for v in c) for c in clips]
    if args.normalize == "clip":
        gains = [PEAK_TARGET / pk if pk > 0 else 1.0 for pk in peaks]
    elif args.normalize == "bank":
        g = PEAK_TARGET / max(peaks) if max(peaks) > 0 else 1.0
        gains = [g] * len(clips)
    else:
        gains = [1.0] * len(clips)
        for path, pk in zip(files, peaks):
            if pk > 1.0:
                print("warning: %s: peak %.2f exceeds full scale; will clip" % (path, pk),
                      file=sys.stderr)
    for path, pk in zip(files, peaks):
        if pk == 0:
            print("warning: %s is silent" % path, file=sys.stderr)
    clips = [[v * g for v in c] if g != 1.0 else c for c, g in zip(clips, gains)]

    # build index table + data
    entries, blobs, pos = [], [], 0
    for c in clips:
        entries.append((pos, pos + len(c)))
        blobs.append(encode_int16(c))
        pos += len(c)
    seen_names = {}
    names = [pack_name(os.path.basename(path), i, seen_names)
             for i, path in enumerate(files)]
    header = struct.pack(HEADER_FMT, len(clips), args.rate)
    table = b"".join(struct.pack(ENTRY_FMT, s, e, n)
                      for (s, e), n in zip(entries, names))
    total = len(header) + len(table) + sum(len(b) for b in blobs)

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(table)
        for b in blobs:
            f.write(b)

    # report
    addr = int(args.address, 0)
    root = args.input if os.path.isdir(args.input) else os.path.dirname(args.input)
    print("%d clip(s) -> %s  (%d Hz mono, int16, fades %.1f/%.1f ms, normalize: %s)"
          % (len(clips), out_path, args.rate, args.fade_in, args.fade_out, args.normalize))
    print("\n  idx     start       end   secs   gain  file")
    for i, ((s, e), g, path) in enumerate(zip(entries, gains, files)):
        gain_db = 20 * math.log10(g) if g > 0 else 0.0
        print("  %3d %9d %9d %6.2f %+5.1f  %s"
              % (i, s, e, (e - s) / args.rate, gain_db, os.path.relpath(path, root or ".")))
    print("\n  header %d B + table %d B + data %d B = %d bytes; %d samples total"
          % (len(header), len(table), total - len(header) - len(table), total, pos))

    if addr % 0x10000:
        print("warning: %#x is not 64 kB aligned; QSPI sectors erase in 64 kB blocks"
              % addr, file=sys.stderr)
    if addr + total > QSPI_END:
        print("warning: bank would run past the end of QSPI (%#x)" % QSPI_END, file=sys.stderr)

    end_addr = addr + total
    print("  bank will occupy %#x - %#x"
          % (addr, end_addr))

if __name__ == "__main__":
    main()