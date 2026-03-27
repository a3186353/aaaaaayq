#!/usr/bin/env python3
"""
GGELUA Opcode Shuffle & Dummy Injection Tool
=============================================
Randomizes Lua 5.4.8 opcode ordering and injects mimicry dummy opcodes
to defeat static analysis and decompilation.

Usage:
    python shuffle_opcodes.py [--seed N] [--num-dummies N] [--verify] [--dry-run]

Output files (relative to lua source dir):
    lopcodes.h   - Rewritten enum + macros
    lopcodes.c   - Rewritten opmodes array
    ljumptab.h   - Rewritten GCC computed goto table
    lvm.c        - Dummy vmcase branches injected
    opcode_map.json - Mapping log (gitignored)
"""

import argparse
import json
import os
import random
import re
import sys
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).parent.resolve()
LUA_DIR = SCRIPT_DIR.parent / "Dependencies" / "lua"

NUM_DUMMIES_DEFAULT = 12  # Number of dummy opcodes to inject

# The real opcodes in standard Lua 5.4.8 order (83 opcodes, 0-82)
STANDARD_OPCODES = [
    "OP_MOVE", "OP_LOADI", "OP_LOADF", "OP_LOADK", "OP_LOADKX",
    "OP_LOADFALSE", "OP_LFALSESKIP", "OP_LOADTRUE", "OP_LOADNIL",
    "OP_GETUPVAL", "OP_SETUPVAL",
    "OP_GETTABUP", "OP_GETTABLE", "OP_GETI", "OP_GETFIELD",
    "OP_SETTABUP", "OP_SETTABLE", "OP_SETI", "OP_SETFIELD",
    "OP_NEWTABLE", "OP_SELF",
    "OP_ADDI",
    "OP_ADDK", "OP_SUBK", "OP_MULK", "OP_MODK", "OP_POWK", "OP_DIVK", "OP_IDIVK",
    "OP_BANDK", "OP_BORK", "OP_BXORK",
    "OP_SHRI", "OP_SHLI",
    "OP_ADD", "OP_SUB", "OP_MUL", "OP_MOD", "OP_POW", "OP_DIV", "OP_IDIV",
    "OP_BAND", "OP_BOR", "OP_BXOR", "OP_SHL", "OP_SHR",
    "OP_MMBIN", "OP_MMBINI", "OP_MMBINK",
    "OP_UNM", "OP_BNOT", "OP_NOT", "OP_LEN",
    "OP_CONCAT",
    "OP_CLOSE", "OP_TBC",
    "OP_JMP",
    "OP_EQ", "OP_LT", "OP_LE",
    "OP_EQK", "OP_EQI", "OP_LTI", "OP_LEI", "OP_GTI", "OP_GEI",
    "OP_TEST", "OP_TESTSET",
    "OP_CALL", "OP_TAILCALL",
    "OP_RETURN", "OP_RETURN0", "OP_RETURN1",
    "OP_FORLOOP", "OP_FORPREP",
    "OP_TFORPREP", "OP_TFORCALL", "OP_TFORLOOP",
    "OP_SETLIST",
    "OP_CLOSURE",
    "OP_VARARG", "OP_VARARGPREP",
    "OP_EXTRAARG",
]

# Standard opmode definitions: (mm, ot, it, t, a, mode_str)
STANDARD_OPMODES = {
    "OP_MOVE":       (0, 0, 0, 0, 1, "iABC"),
    "OP_LOADI":      (0, 0, 0, 0, 1, "iAsBx"),
    "OP_LOADF":      (0, 0, 0, 0, 1, "iAsBx"),
    "OP_LOADK":      (0, 0, 0, 0, 1, "iABx"),
    "OP_LOADKX":     (0, 0, 0, 0, 1, "iABx"),
    "OP_LOADFALSE":  (0, 0, 0, 0, 1, "iABC"),
    "OP_LFALSESKIP": (0, 0, 0, 0, 1, "iABC"),
    "OP_LOADTRUE":   (0, 0, 0, 0, 1, "iABC"),
    "OP_LOADNIL":    (0, 0, 0, 0, 1, "iABC"),
    "OP_GETUPVAL":   (0, 0, 0, 0, 1, "iABC"),
    "OP_SETUPVAL":   (0, 0, 0, 0, 0, "iABC"),
    "OP_GETTABUP":   (0, 0, 0, 0, 1, "iABC"),
    "OP_GETTABLE":   (0, 0, 0, 0, 1, "iABC"),
    "OP_GETI":       (0, 0, 0, 0, 1, "iABC"),
    "OP_GETFIELD":   (0, 0, 0, 0, 1, "iABC"),
    "OP_SETTABUP":   (0, 0, 0, 0, 0, "iABC"),
    "OP_SETTABLE":   (0, 0, 0, 0, 0, "iABC"),
    "OP_SETI":       (0, 0, 0, 0, 0, "iABC"),
    "OP_SETFIELD":   (0, 0, 0, 0, 0, "iABC"),
    "OP_NEWTABLE":   (0, 0, 0, 0, 1, "iABC"),
    "OP_SELF":       (0, 0, 0, 0, 1, "iABC"),
    "OP_ADDI":       (0, 0, 0, 0, 1, "iABC"),
    "OP_ADDK":       (0, 0, 0, 0, 1, "iABC"),
    "OP_SUBK":       (0, 0, 0, 0, 1, "iABC"),
    "OP_MULK":       (0, 0, 0, 0, 1, "iABC"),
    "OP_MODK":       (0, 0, 0, 0, 1, "iABC"),
    "OP_POWK":       (0, 0, 0, 0, 1, "iABC"),
    "OP_DIVK":       (0, 0, 0, 0, 1, "iABC"),
    "OP_IDIVK":      (0, 0, 0, 0, 1, "iABC"),
    "OP_BANDK":      (0, 0, 0, 0, 1, "iABC"),
    "OP_BORK":       (0, 0, 0, 0, 1, "iABC"),
    "OP_BXORK":      (0, 0, 0, 0, 1, "iABC"),
    "OP_SHRI":       (0, 0, 0, 0, 1, "iABC"),
    "OP_SHLI":       (0, 0, 0, 0, 1, "iABC"),
    "OP_ADD":        (0, 0, 0, 0, 1, "iABC"),
    "OP_SUB":        (0, 0, 0, 0, 1, "iABC"),
    "OP_MUL":        (0, 0, 0, 0, 1, "iABC"),
    "OP_MOD":        (0, 0, 0, 0, 1, "iABC"),
    "OP_POW":        (0, 0, 0, 0, 1, "iABC"),
    "OP_DIV":        (0, 0, 0, 0, 1, "iABC"),
    "OP_IDIV":       (0, 0, 0, 0, 1, "iABC"),
    "OP_BAND":       (0, 0, 0, 0, 1, "iABC"),
    "OP_BOR":        (0, 0, 0, 0, 1, "iABC"),
    "OP_BXOR":       (0, 0, 0, 0, 1, "iABC"),
    "OP_SHL":        (0, 0, 0, 0, 1, "iABC"),
    "OP_SHR":        (0, 0, 0, 0, 1, "iABC"),
    "OP_MMBIN":      (1, 0, 0, 0, 0, "iABC"),
    "OP_MMBINI":     (1, 0, 0, 0, 0, "iABC"),
    "OP_MMBINK":     (1, 0, 0, 0, 0, "iABC"),
    "OP_UNM":        (0, 0, 0, 0, 1, "iABC"),
    "OP_BNOT":       (0, 0, 0, 0, 1, "iABC"),
    "OP_NOT":        (0, 0, 0, 0, 1, "iABC"),
    "OP_LEN":        (0, 0, 0, 0, 1, "iABC"),
    "OP_CONCAT":     (0, 0, 0, 0, 1, "iABC"),
    "OP_CLOSE":      (0, 0, 0, 0, 0, "iABC"),
    "OP_TBC":        (0, 0, 0, 0, 0, "iABC"),
    "OP_JMP":        (0, 0, 0, 0, 0, "isJ"),
    "OP_EQ":         (0, 0, 0, 1, 0, "iABC"),
    "OP_LT":         (0, 0, 0, 1, 0, "iABC"),
    "OP_LE":         (0, 0, 0, 1, 0, "iABC"),
    "OP_EQK":        (0, 0, 0, 1, 0, "iABC"),
    "OP_EQI":        (0, 0, 0, 1, 0, "iABC"),
    "OP_LTI":        (0, 0, 0, 1, 0, "iABC"),
    "OP_LEI":        (0, 0, 0, 1, 0, "iABC"),
    "OP_GTI":        (0, 0, 0, 1, 0, "iABC"),
    "OP_GEI":        (0, 0, 0, 1, 0, "iABC"),
    "OP_TEST":       (0, 0, 0, 1, 0, "iABC"),
    "OP_TESTSET":    (0, 0, 0, 1, 1, "iABC"),
    "OP_CALL":       (0, 1, 1, 0, 1, "iABC"),
    "OP_TAILCALL":   (0, 1, 1, 0, 1, "iABC"),
    "OP_RETURN":     (0, 0, 1, 0, 0, "iABC"),
    "OP_RETURN0":    (0, 0, 0, 0, 0, "iABC"),
    "OP_RETURN1":    (0, 0, 0, 0, 0, "iABC"),
    "OP_FORLOOP":    (0, 0, 0, 0, 1, "iABx"),
    "OP_FORPREP":    (0, 0, 0, 0, 1, "iABx"),
    "OP_TFORPREP":   (0, 0, 0, 0, 0, "iABx"),
    "OP_TFORCALL":   (0, 0, 0, 0, 0, "iABC"),
    "OP_TFORLOOP":   (0, 0, 0, 0, 1, "iABx"),
    "OP_SETLIST":    (0, 0, 1, 0, 0, "iABC"),
    "OP_CLOSURE":    (0, 0, 0, 0, 1, "iABx"),
    "OP_VARARG":     (0, 1, 0, 0, 1, "iABC"),
    "OP_VARARGPREP": (0, 0, 1, 0, 1, "iABC"),
    "OP_EXTRAARG":   (0, 0, 0, 0, 0, "iAx"),
}

# ---------------------------------------------------------------------------
# Frozen Groups: opcodes that MUST remain contiguous in the enum.
# lcode.c uses arithmetic offsets (binopr2op, unopr2op) to map BinOpr/UnOpr
# to OpCode, assuming these groups are consecutive. Shuffling them apart
# causes the compiler to emit wrong opcodes -> VM crash.
# ---------------------------------------------------------------------------

FROZEN_GROUPS = [
    # Arithmetic K-operand opcodes: binopr2op(opr, OPR_ADD, OP_ADDK)
    ["OP_ADDK", "OP_SUBK", "OP_MULK", "OP_MODK", "OP_POWK", "OP_DIVK", "OP_IDIVK"],
    # Bitwise K-operand opcodes (continue from ADDK group for binopr2op)
    ["OP_BANDK", "OP_BORK", "OP_BXORK"],
    # Shift immediate opcodes
    ["OP_SHRI", "OP_SHLI"],
    # Arithmetic register opcodes: binopr2op(opr, OPR_ADD, OP_ADD)
    ["OP_ADD", "OP_SUB", "OP_MUL", "OP_MOD", "OP_POW", "OP_DIV", "OP_IDIV",
     "OP_BAND", "OP_BOR", "OP_BXOR", "OP_SHL", "OP_SHR"],
    # Metamethod binary opcodes (must follow arithmetic for finishbinexpval)
    ["OP_MMBIN", "OP_MMBINI", "OP_MMBINK"],
    # Unary opcodes: unopr2op(opr) = (opr - OPR_MINUS) + OP_UNM
    ["OP_UNM", "OP_BNOT", "OP_NOT", "OP_LEN"],
    # Comparison register opcodes: binopr2op(opr, OPR_LT, OP_LT)
    ["OP_EQ", "OP_LT", "OP_LE"],
    # Comparison immediate opcodes: binopr2op(opr, OPR_LT, OP_LTI)
    # and binopr2op(opr, OPR_LT, OP_GTI)
    ["OP_LTI", "OP_LEI", "OP_GTI", "OP_GEI"],
]

# ---------------------------------------------------------------------------
# Mimicry Dummy Opcode Templates
# ---------------------------------------------------------------------------
# Each template looks like a real opcode handler but produces no observable
# side effects. Uses volatile reads and dead writes to prevent optimization.
# The key trick: read from real registers, do real-looking computation,
# but write results to a local variable that is never used.

DUMMY_TEMPLATES = [
    # Mimics OP_ADD - arithmetic with two registers
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *v1 = vRB(i);
        TValue *v2 = vRC(i);
        if (ttisinteger(v1) && ttisinteger(v2)) {{
          lua_Integer i1 = ivalue(v1); lua_Integer i2 = ivalue(v2);
          lua_Integer _sink = intop(+, i1, i2);
          (void)_sink; (void)ra;
        }}
        vmbreak;
      }}''',
    # Mimics OP_GETTABLE - table access pattern
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        const TValue *slot;
        TValue *rb = vRB(i);
        TValue *rc = vRC(i);
        if (ttisinteger(rc)) {{
          lua_Unsigned n = (lua_Unsigned)ivalue(rc);
          (void)luaV_fastgeti(L, rb, n, slot);
        }}
        (void)ra; (void)slot;
        vmbreak;
      }}''',
    # Mimics OP_SETFIELD - field write pattern
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        const TValue *slot;
        if (ttistable(s2v(ra))) {{
          Table *h = hvalue(s2v(ra));
          (void)h;
        }}
        (void)rb; (void)slot;
        vmbreak;
      }}''',
    # Mimics OP_LOADK - constant load
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        int bx = GETARG_Bx(i);
        TValue *rb = k + bx;
        (void)ra; (void)rb;
        vmbreak;
      }}''',
    # Mimics OP_CONCAT - string concat pattern
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        int b = GETARG_B(i);
        int _count = cast_int(b);
        (void)ra; (void)_count;
        vmbreak;
      }}''',
    # Mimics OP_MOVE - register move
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        StkId rb = RB(i);
        (void)ra; (void)rb;
        vmbreak;
      }}''',
    # Mimics OP_LEN - length operator
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (ttistable(rb)) {{
          Table *h = hvalue(rb);
          lua_Unsigned _len = luaH_getn(h);
          (void)_len;
        }}
        (void)ra;
        vmbreak;
      }}''',
    # Mimics OP_EQ - equality test
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int _eq = luaV_equalobj(NULL, s2v(ra), rb);
        (void)_eq;
        vmbreak;
      }}''',
    # Mimics OP_FORPREP - for loop preparation
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *plimit = s2v(ra + 1);
        if (ttisinteger(s2v(ra)) && ttisinteger(plimit)) {{
          lua_Integer _init = ivalue(s2v(ra));
          (void)_init;
        }}
        vmbreak;
      }}''',
    # Mimics OP_CLOSURE - closure creation read
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        int bx = GETARG_Bx(i);
        Proto *_p = cl->p->p[bx < cl->p->sizep ? bx : 0];
        (void)ra; (void)_p;
        vmbreak;
      }}''',
    # Mimics OP_SELF - method lookup pattern
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        const TValue *slot;
        if (ttistable(rb)) {{
          Table *h = hvalue(rb);
          (void)h;
        }}
        (void)ra; (void)rb; (void)slot;
        vmbreak;
      }}''',
    # Mimics OP_GETUPVAL - upvalue read
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        int b = GETARG_B(i);
        int _idx = b < cl->nupvalues ? b : 0;
        TValue *_uv = cl->upvals[_idx]->v.p;
        (void)ra; (void)_uv;
        vmbreak;
      }}''',
    # Mimics OP_SUB - subtraction
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *v1 = vRB(i);
        TValue *v2 = vRC(i);
        if (ttisinteger(v1) && ttisinteger(v2)) {{
          lua_Integer _r = intop(-, ivalue(v1), ivalue(v2));
          (void)_r;
        }}
        (void)ra;
        vmbreak;
      }}''',
    # Mimics OP_TEST - boolean test
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        int cond = !l_isfalse(s2v(ra));
        int _k = GETARG_k(i);
        (void)cond; (void)_k;
        vmbreak;
      }}''',
    # Mimics OP_BAND - bitwise AND
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *v1 = vRB(i);
        TValue *v2 = vRC(i);
        lua_Integer i1, i2;
        if (tointegerns(v1, &i1) && tointegerns(v2, &i2)) {{
          lua_Integer _r = intop(&, i1, i2);
          (void)_r;
        }}
        (void)ra;
        vmbreak;
      }}''',
    # Mimics OP_SHRI - shift right
    '''\
      vmcase({name}) {{
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = GETARG_sC(i);
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {{
          lua_Integer _r = luaV_shiftl(ib, -ic);
          (void)_r;
        }}
        (void)ra;
        vmbreak;
      }}''',
]


# ---------------------------------------------------------------------------
# Core Logic
# ---------------------------------------------------------------------------

def generate_shuffled_opcodes(seed: int, num_dummies: int):
    """Generate a shuffled opcode list with frozen groups preserved.

    Frozen groups are treated as atomic units during shuffle: their
    internal order is preserved, but their position among other opcodes
    is randomized. This keeps binopr2op() / unopr2op() arithmetic
    offset mappings in lcode.c valid.
    """
    rng = random.Random(seed)

    # Create dummy opcode names
    dummies = [f"OP_DUMMY_{i:02d}" for i in range(num_dummies)]

    # Collect all frozen opcode names
    frozen_set = set()
    for group in FROZEN_GROUPS:
        for op in group:
            frozen_set.add(op)

    # OP_EXTRAARG must always be last
    real_ops = STANDARD_OPCODES[:-1]  # exclude OP_EXTRAARG

    # Separate into: frozen groups (as atomic units) + loose opcodes
    loose_ops = [op for op in real_ops if op not in frozen_set] + dummies
    # Build items list: each frozen group is a single item (list), each loose op is a single item (list of 1)
    items = []
    for op in loose_ops:
        items.append([op])
    for group in FROZEN_GROUPS:
        items.append(list(group))

    # Shuffle the items
    rng.shuffle(items)

    # Flatten
    all_ops = []
    for item in items:
        all_ops.extend(item)

    # Append OP_EXTRAARG at the end
    all_ops.append("OP_EXTRAARG")

    return all_ops, dummies


def generate_dummy_opmodes(dummies: list, rng: random.Random):
    """Generate plausible-looking opmodes for dummy opcodes."""
    # Pick from common opmode patterns to look realistic
    patterns = [
        (0, 0, 0, 0, 1, "iABC"),   # like OP_MOVE, OP_ADD
        (0, 0, 0, 0, 1, "iABx"),   # like OP_LOADK
        (0, 0, 0, 1, 0, "iABC"),   # like OP_EQ
        (0, 0, 0, 0, 0, "iABC"),   # like OP_SETUPVAL
    ]
    result = {}
    for d in dummies:
        result[d] = rng.choice(patterns)
    return result


def generate_dummy_vmcases(dummies: list, rng: random.Random):
    """Generate mimicry vmcase code for each dummy opcode."""
    cases = {}
    available = list(DUMMY_TEMPLATES)
    for d in dummies:
        # Pick a random template (allow reuse after all used)
        if not available:
            available = list(DUMMY_TEMPLATES)
        tpl = rng.choice(available)
        available.remove(tpl)
        cases[d] = tpl.format(name=d)
    return cases


# ---------------------------------------------------------------------------
# File Generators
# ---------------------------------------------------------------------------

def generate_lopcodes_h_enum(shuffled_ops: list, original_content: str) -> str:
    """Replace the OpCode enum in lopcodes.h."""
    # Build new enum body
    enum_lines = []
    for idx, op in enumerate(shuffled_ops):
        if op.startswith("OP_DUMMY_"):
            comment = f"/*\tDummy - mimicry opcode (anti-analysis)\t\t\t*/"
        elif op in STANDARD_OPCODES:
            # Find original comment from standard list
            comment = get_original_comment(op, original_content)
        else:
            comment = ""

        if idx == 0:
            enum_lines.append(f"{op},{comment}")
        else:
            enum_lines.append(f"{op},{comment}")

    # Replace enum block (first OP_ may not be OP_MOVE after a previous shuffle)
    enum_pattern = re.compile(
        r'(typedef\s+enum\s*\{[^}]*?)\n'
        r'(OP_\w+,.*?OP_EXTRAARG[^}]*)'
        r'(\}\s*OpCode\s*;)',
        re.DOTALL
    )

    new_enum_body = "\n".join(enum_lines)

    def replace_enum(m):
        return f"typedef enum {{\n/*----------------------------------------------------------------------\n  name\t\targs\tdescription\n------------------------------------------------------------------------*/\n{new_enum_body}\n}} OpCode;"

    result = enum_pattern.sub(replace_enum, original_content)
    return result


def get_original_comment(opname: str, content: str) -> str:
    """Extract the original comment for an opcode from the source."""
    pattern = re.compile(rf'{re.escape(opname)},(/\*.*?\*/)', re.DOTALL)
    m = pattern.search(content)
    return m.group(1) if m else ""


def generate_lopcodes_c(shuffled_ops: list, dummy_opmodes: dict) -> str:
    """Generate the complete lopcodes.c with reordered opmodes."""
    lines = [
        "/*",
        "** $Id: lopcodes.c $",
        "** Opcodes for Lua virtual machine",
        "** See Copyright Notice in lua.h",
        "*/",
        "",
        "#define lopcodes_c",
        "#define LUA_CORE",
        "",
        '#include "lprefix.h"',
        "",
        "",
        '#include "lopcodes.h"',
        "",
        "",
        "/* ORDER OP */",
        "",
        "LUAI_DDEF const lu_byte luaP_opmodes[NUM_OPCODES] = {",
        "/*       MM OT IT T  A  mode\t\t   opcode  */",
    ]

    for idx, op in enumerate(shuffled_ops):
        if op in STANDARD_OPMODES:
            mm, ot, it, t, a, mode = STANDARD_OPMODES[op]
        elif op in dummy_opmodes:
            mm, ot, it, t, a, mode = dummy_opmodes[op]
        else:
            raise ValueError(f"Unknown opcode: {op}")

        prefix = "  " if idx == 0 else " ,"
        line = f"{prefix}opmode({mm}, {ot}, {it}, {t}, {a}, {mode})\t\t/* {op} */"
        lines.append(line)

    lines.append("};")
    lines.append("")
    lines.append("")

    return "\n".join(lines)


def generate_ljumptab_h(shuffled_ops: list) -> str:
    """Generate ljumptab.h with reordered dispatch table."""
    lines = [
        "/*",
        "** $Id: ljumptab.h $",
        "** Jump Table for the Lua interpreter",
        "** See Copyright Notice in lua.h",
        "*/",
        "",
        "",
        "#undef vmdispatch",
        "#undef vmcase",
        "#undef vmbreak",
        "",
        '#define vmdispatch(x)     goto *disptab[x];',
        "",
        "#define vmcase(l)     L_##l:",
        "",
        "#define vmbreak\t\tvmfetch(); vmdispatch(GET_OPCODE(i));",
        "",
        "",
        "static const void *const disptab[NUM_OPCODES] = {",
        "",
    ]

    for idx, op in enumerate(shuffled_ops):
        comma = "," if idx < len(shuffled_ops) - 1 else ""
        lines.append(f"&&L_{op}{comma}")

    lines.append("")
    lines.append("};")
    lines.append("")

    return "\n".join(lines)


def generate_lopnames_h(shuffled_ops: list) -> str:
    """Generate lopnames.h with opcode names in shuffled order."""
    lines = [
        "/*",
        "** $Id: lopnames.h $",
        "** Opcode names",
        "** See Copyright Notice in lua.h",
        "*/",
        "",
        "#if !defined(lopnames_h)",
        "#define lopnames_h",
        "",
        "#include <stddef.h>",
        "",
        "",
        "/* ORDER OP */",
        "",
        "static const char *const opnames[] = {",
    ]
    for op in shuffled_ops:
        # Strip "OP_" prefix for the name string
        name = op.replace("OP_", "")
        lines.append(f'  "{name}",')
    lines.append("  NULL")
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def inject_dummy_cases_into_lvm(lvm_content: str, dummy_cases: dict) -> str:
    """Inject dummy vmcase blocks into lvm.c before the last vmcase (OP_EXTRAARG)."""
    # Find the OP_EXTRAARG case and inject dummies before it
    marker = "      vmcase(OP_EXTRAARG) {"

    if marker not in lvm_content:
        # Try alternate formatting
        marker = "vmcase(OP_EXTRAARG)"
        if marker not in lvm_content:
            raise ValueError("Cannot find OP_EXTRAARG vmcase in lvm.c")

    # Build injection block
    injection_lines = [
        "",
        "/* ========== GGELUA Anti-Analysis: Mimicry Dummy Opcodes ========== */",
    ]
    for op_name, case_code in sorted(dummy_cases.items()):
        injection_lines.append(case_code)
    injection_lines.append("/* ========== End Mimicry Dummy Opcodes ========== */")
    injection_lines.append("")

    injection = "\n".join(injection_lines)

    # Remove any previously injected dummy block
    lvm_content = re.sub(
        r'/\* =+ GGELUA Anti-Analysis: Mimicry Dummy Opcodes =+ \*/.*?'
        r'/\* =+ End Mimicry Dummy Opcodes =+ \*/',
        '',
        lvm_content,
        flags=re.DOTALL
    )

    # Inject before OP_EXTRAARG
    lvm_content = lvm_content.replace(
        marker,
        injection + "\n" + marker
    )

    return lvm_content


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

def verify(shuffled_ops: list, dummies: list):
    """Verify the generated opcode list for consistency."""
    errors = []

    # 1. All standard opcodes must be present
    for op in STANDARD_OPCODES:
        if op not in shuffled_ops:
            errors.append(f"MISSING standard opcode: {op}")

    # 2. All dummies must be present
    for d in dummies:
        if d not in shuffled_ops:
            errors.append(f"MISSING dummy opcode: {d}")

    # 3. No duplicates
    if len(shuffled_ops) != len(set(shuffled_ops)):
        seen = set()
        for op in shuffled_ops:
            if op in seen:
                errors.append(f"DUPLICATE opcode: {op}")
            seen.add(op)

    # 4. OP_EXTRAARG must be last
    if shuffled_ops[-1] != "OP_EXTRAARG":
        errors.append(f"OP_EXTRAARG is not last! Position: {shuffled_ops.index('OP_EXTRAARG')}")

    # 5. NUM_OPCODES check
    expected = len(STANDARD_OPCODES) + len(dummies)
    if len(shuffled_ops) != expected:
        errors.append(f"NUM_OPCODES mismatch: got {len(shuffled_ops)}, expected {expected}")

    # 6. Frozen groups must remain contiguous and in order
    for group in FROZEN_GROUPS:
        indices = [shuffled_ops.index(op) for op in group]
        for i in range(1, len(indices)):
            if indices[i] != indices[i-1] + 1:
                errors.append(
                    f"FROZEN GROUP BROKEN: {group[i-1]}({indices[i-1]}) and "
                    f"{group[i]}({indices[i]}) are not contiguous"
                )
                break

    # 7. Total opcodes must fit in 7-bit opcode field (max 128)
    if len(shuffled_ops) > 128:
        errors.append(f"Too many opcodes ({len(shuffled_ops)}) for 7-bit opcode field (max 128)")

    return errors


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="GGELUA Opcode Shuffle & Dummy Injection Tool"
    )
    parser.add_argument("--seed", type=int, default=None,
                        help="Random seed (default: current timestamp)")
    parser.add_argument("--num-dummies", type=int, default=NUM_DUMMIES_DEFAULT,
                        help=f"Number of dummy opcodes (default: {NUM_DUMMIES_DEFAULT})")
    parser.add_argument("--verify", action="store_true",
                        help="Verify existing generated files")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print changes without writing files")
    parser.add_argument("--lua-dir", type=str, default=str(LUA_DIR),
                        help="Path to Lua source directory")
    args = parser.parse_args()

    lua_dir = Path(args.lua_dir)
    if not lua_dir.exists():
        print(f"ERROR: Lua directory not found: {lua_dir}")
        sys.exit(1)

    seed = args.seed if args.seed is not None else int(time.time())
    print(f"=== GGELUA Opcode Shuffle ===")
    print(f"Seed: {seed}")
    print(f"Dummy opcodes: {args.num_dummies}")
    print(f"Lua dir: {lua_dir}")
    print()

    rng = random.Random(seed)

    # Generate shuffled opcodes
    shuffled_ops, dummies = generate_shuffled_opcodes(seed, args.num_dummies)
    dummy_opmodes = generate_dummy_opmodes(dummies, rng)
    dummy_cases = generate_dummy_vmcases(dummies, rng)

    total = len(shuffled_ops)
    print(f"Total opcodes: {total} ({len(STANDARD_OPCODES)} real + {len(dummies)} dummy)")
    print(f"OP_EXTRAARG position: {shuffled_ops.index('OP_EXTRAARG')} (last={total-1})")

    # Verify
    errors = verify(shuffled_ops, dummies)
    if errors:
        print("\n!!! VERIFICATION ERRORS !!!")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print("Verification: PASSED [OK]")

    if args.verify:
        return

    # Read original files
    lopcodes_h_path = lua_dir / "lopcodes.h"
    lopcodes_c_path = lua_dir / "lopcodes.c"
    ljumptab_h_path = lua_dir / "ljumptab.h"
    lopnames_h_path = lua_dir / "lopnames.h"
    lvm_c_path = lua_dir / "lvm.c"

    lopcodes_h = lopcodes_h_path.read_text(encoding='utf-8')
    lvm_c = lvm_c_path.read_text(encoding='utf-8')

    # Generate new content
    new_lopcodes_h = generate_lopcodes_h_enum(shuffled_ops, lopcodes_h)
    # Fix SIZE_OP/SIZE_C: 7-bit opcode is sufficient for <= 128 opcodes
    new_lopcodes_h = re.sub(
        r'#define SIZE_C\s+\d+', '#define SIZE_C\t\t8', new_lopcodes_h)
    new_lopcodes_h = re.sub(
        r'#define SIZE_OP\s+\d+', '#define SIZE_OP\t\t7', new_lopcodes_h)
    new_lopcodes_c = generate_lopcodes_c(shuffled_ops, dummy_opmodes)
    new_ljumptab_h = generate_ljumptab_h(shuffled_ops)
    new_lopnames_h = generate_lopnames_h(shuffled_ops)
    new_lvm_c = inject_dummy_cases_into_lvm(lvm_c, dummy_cases)

    if args.dry_run:
        print("\n=== DRY RUN: Changes not written ===")
        print(f"\nNew opcode order (first 20):")
        for idx, op in enumerate(shuffled_ops[:20]):
            tag = " [DUMMY]" if op in dummies else ""
            print(f"  {idx:3d}: {op}{tag}")
        print(f"  ... ({total - 20} more)")
        print(f"\nDummy opcode mimicry types:")
        for d in sorted(dummies):
            # Extract template type from first line
            code = dummy_cases[d]
            print(f"  {d}: {len(code.splitlines())} lines of mimicry code")
        return

    # Write files
    print("\nWriting files...")
    lopcodes_h_path.write_text(new_lopcodes_h, encoding='utf-8')
    print(f"  [OK] {lopcodes_h_path}")

    lopcodes_c_path.write_text(new_lopcodes_c, encoding='utf-8')
    print(f"  [OK] {lopcodes_c_path}")

    ljumptab_h_path.write_text(new_ljumptab_h, encoding='utf-8')
    print(f"  [OK] {ljumptab_h_path}")

    lopnames_h_path.write_text(new_lopnames_h, encoding='utf-8')
    print(f"  [OK] {lopnames_h_path}")

    lvm_c_path.write_text(new_lvm_c, encoding='utf-8')
    print(f"  [OK] {lvm_c_path}")

    # Write mapping log
    map_path = SCRIPT_DIR / "opcode_map.json"
    mapping = {
        "seed": seed,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "num_real": len(STANDARD_OPCODES),
        "num_dummy": len(dummies),
        "total": total,
        "order": {op: idx for idx, op in enumerate(shuffled_ops)},
        "dummies": dummies,
        "standard_to_shuffled": {
            op: shuffled_ops.index(op) for op in STANDARD_OPCODES
        },
    }
    map_path.write_text(json.dumps(mapping, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f"  [OK] {map_path}")

    print(f"\n=== Done! Rebuild the engine to apply changes. ===")


if __name__ == "__main__":
    main()
