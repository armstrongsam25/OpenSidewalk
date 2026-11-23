#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pack an animated GIF for the optional round display."""
from struct import pack

SIZE = 144
FRAME_MS = 125
MAX_DURATION_MS = 8_000


def pack_button_gif(source, destination):
    try:
        from PIL import Image, ImageOps, ImageSequence
    except ImportError:
        raise SystemExit("Install Pillow to build with --button-gif: python -m pip install Pillow")

    # Two-byte duration, followed by frames containing an RGB565 palette and 4-bit indices.
    data = bytearray(2)
    duration = 0
    next_frame = 0
    with Image.open(source) as gif:
        if gif.format != "GIF" or not gif.is_animated:
            raise ValueError("--button-gif requires an animated GIF")
        for frame in ImageSequence.Iterator(gif):
            duration = min(duration + (frame.info.get("duration") or 100), MAX_DURATION_MS)
            if next_frame < duration:
                scaled = ImageOps.contain(frame.convert("RGBA"), (SIZE, SIZE), Image.Resampling.LANCZOS)
                canvas = Image.new("RGB", (SIZE, SIZE))
                offset = ((SIZE - scaled.width) // 2, (SIZE - scaled.height) // 2)
                canvas.paste(scaled, offset, scaled.getchannel("A"))
                indexed = canvas.quantize(colors=16, method=Image.Quantize.MEDIANCUT)
                palette = bytes(indexed.getpalette()[:48]).ljust(48, b"\0")
                packed_frame = bytearray()
                for r, g, b in zip(palette[::3], palette[1::3], palette[2::3]):
                    packed_frame.extend(pack(">H", (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)))
                pixels = indexed.tobytes()
                packed_frame.extend((hi << 4) | lo for hi, lo in zip(pixels[::2], pixels[1::2]))
                samples = (duration - next_frame + FRAME_MS - 1) // FRAME_MS
                data.extend(packed_frame * samples)
                next_frame += samples * FRAME_MS
            if duration == MAX_DURATION_MS:
                break

    data[:2] = pack(">H", duration)
    destination.write_bytes(data)
