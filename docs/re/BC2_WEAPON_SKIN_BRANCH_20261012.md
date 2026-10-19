> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Weapon skin submission branch

Target: `BFBC2Game.exe`, x86, image base `0x00400000`, SHA-256
`c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
The retained REA/Ghidra `native-skin-seam-20261011/skin-draw.txt` identifies
the containing function at `0x005d73d0`.

| Submit call / return VA | Request palette | Request LOD array / count |
| --- | --- | --- |
| `0x005d7910` / `0x005d7915` | `+0x84` | `+0x94` / `+0x98` |
| `0x005d7960` / `0x005d7965` | `+0x80` | `+0x8c` / `+0x90` |

At `0x005d78f0` the alternate loop loads its LOD pointer from `[ESI+0x94]`;
after calling `0x00970870`, `0x005d791b` compares against `[ESI+0x98]`.
The ordinary loop loads `[ESI+0x8c]` at `0x005d7940` and compares against
`[ESI+0x90]` at `0x005d796b`. The preceding palette loads use `+0x84`
and `+0x80`, respectively. These are observed native instructions, not offsets
inferred from descriptor names.

`WeaponAssociated` had selected alternate fields for return address
`0x005d7965`, reversing these branches. It now selects alternate fields for
`0x005d7915`. Palette, exact LOD membership, owner, pass, descriptor reread,
request reread and freshness guards remain intact. The playtest's 4,877
pre-scope prop rejections are consistent with this mismatch; zero prop draws
without an active prop does not itself prove a rendering failure. Runtime
confirmation of this correction remains pending.
