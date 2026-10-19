"""Prefix an FVR_BC2_INPUT tape with a short right-trigger burst so a full
magazine drops below capacity before the recorded reload. Every other record
is copied unchanged; the burst reuses the first idle record's poses."""
import sys
from pathlib import Path

src, dst = Path(sys.argv[1]), Path(sys.argv[2])
burst_ticks = int(sys.argv[3]) if len(sys.argv) > 3 else 25
lines = src.read_text(encoding="ascii").splitlines()
out, first = [], None
for i, line in enumerate(lines):
    if first is None and line and not line.startswith("#") and not line.startswith("FVR_BC2_INPUT"):
        first = i
        tok = line.split()
        # 20 scalar fields before the hands: TICKS AGE EPOCH FOCUSED HEADVALID FLOOR head(7) referenceHead(7)
        # Hands are FLAGS BUTTONS trigger squeeze stickX stickY grip(7) aim(7): left at 20, right at 40.
        right_buttons, right_trigger = 41, 42
        settle = list(tok); settle[0] = "100"
        fire = list(tok); fire[0] = str(burst_ticks); fire[right_trigger] = "1"
        release = list(tok); release[0] = "150"
        out += ["# prefix: 1 s idle, %d-tick right-trigger burst, 1.5 s idle" % burst_ticks,
                " ".join(settle), " ".join(fire), " ".join(release)]
    out.append(line)
dst.write_text("\n".join(out) + "\n", encoding="ascii")
print(f"wrote {dst} (burst {burst_ticks} ticks at record {first})")
