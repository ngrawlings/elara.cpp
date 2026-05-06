#!/usr/bin/env python3
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OPCODES_X = ROOT / "src" / "opcodes" / "epa_opcodes.x"
ASM_COMPILER = ROOT / "src" / "epa_asm_compiler.c"
SRC_DIR = ROOT / "src"

X_RE = re.compile(
    r'^\s*X\(\s*([A-Z0-9_]+)\s*,\s*(0x[0-9A-Fa-f]+u?)\s*,\s*"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\)'
)

DESC_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(EPA_OP_[A-Z0-9_]+|0)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\{([^}]*)\}\s*,\s*(NULL|helper_emit_[A-Za-z0-9_]+)\s*,'
)

AK_SIZES = {
    "AK_NONE": 0,
    "AK_U8": 1,
    "AK_U16": 2,
    "AK_U32": 4,
    "AK_I32": 4,
    "AK_F32": 4,
    "AK_MODE": 1,
    "AK_REGU8": 1,
    "AK_REGU8_OR_U8": 1,
    "AK_REG4_OR_U8": 1,
}

HELPER_SIZES = {
    "helper_emit_jump_rel32": 4,
    "helper_emit_yield": 1,
}


def parse_int(text: str) -> int:
    text = text.rstrip("uU")
    return int(text, 16 if text.startswith("0x") else 10)


def load_opcodes():
    opcodes = {}
    errors = []
    for lineno, line in enumerate(OPCODES_X.read_text().splitlines(), start=1):
        m = X_RE.match(line)
        if not m:
            continue
        name, _value, mnemonic, param_len = m.groups()
        sym = f"EPA_OP_{name}"
        if sym in opcodes:
            errors.append(f"{OPCODES_X}:{lineno}: duplicate opcode symbol {sym}")
            continue
        opcodes[sym] = {
            "mnemonic": mnemonic,
            "param_len": parse_int(param_len),
            "line": lineno,
        }
    return opcodes, errors


def infer_encoded_size(helper: str, emit_count: int, kinds_text: str):
    if helper in HELPER_SIZES:
        return HELPER_SIZES[helper]
    if helper != "NULL":
        return None

    kinds = [k.strip() for k in kinds_text.split(",") if k.strip()]
    if emit_count == 0:
        return 0
    if len(kinds) < emit_count:
        return None

    total = 0
    for kind in kinds[:emit_count]:
        if kind not in AK_SIZES:
            return None
        total += AK_SIZES[kind]
    return total


def load_runtime_text():
    chunks = []
    for path in sorted(SRC_DIR.rglob("*.c")):
        chunks.append(path.read_text())
    return "\n".join(chunks)


def main() -> int:
    opcodes, errors = load_opcodes()
    runtime_text = load_runtime_text()

    compiler_text = ASM_COMPILER.read_text().splitlines()
    for lineno, line in enumerate(compiler_text, start=1):
        m = DESC_RE.search(line)
        if not m:
            continue

        mnemonic, opcode_sym, _min_args, _max_args, emit_count_s, kinds_text, helper = m.groups()
        if opcode_sym == "0":
            continue

        if opcode_sym not in opcodes:
            errors.append(f"{ASM_COMPILER}:{lineno}: descriptor references unknown opcode {opcode_sym}")
            continue

        opcode = opcodes[opcode_sym]
        if mnemonic != opcode["mnemonic"]:
            errors.append(
                f"{ASM_COMPILER}:{lineno}: mnemonic '{mnemonic}' does not match "
                f"{OPCODES_X}:{opcode['line']} '{opcode['mnemonic']}' for {opcode_sym}"
            )

        emit_count = int(emit_count_s)
        inferred = infer_encoded_size(helper, emit_count, kinds_text)
        if inferred is not None and inferred != opcode["param_len"]:
            errors.append(
                f"{ASM_COMPILER}:{lineno}: encoded size {inferred} does not match "
                f"{OPCODES_X}:{opcode['line']} param_len {opcode['param_len']} for {opcode_sym}"
            )

        if opcode_sym not in runtime_text:
            errors.append(f"{ASM_COMPILER}:{lineno}: no runtime handler reference found for {opcode_sym} under src/")

    if errors:
      print("ISA lint failed:", file=sys.stderr)
      for err in errors:
          print(f"  {err}", file=sys.stderr)
      return 1

    print("ISA lint passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
