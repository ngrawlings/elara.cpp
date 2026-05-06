# If/Else EPA Template Design

This note defines the first control-flow lowering contract for `E -> EPA`.
It is deliberately limited to `if` and `if/else`. Variable referencing and
storage layout are deferred to a later pass.

## Goal

Represent `if` and `if/else` in a way that:

- is explicit enough to lower to EPA blocks
- keeps branch routing integer-based and deterministic
- works with the existing assembler label/jump model
- stays compatible with later CUDA-friendly execution and worker routing

## Core Rule

An `if` statement lowers to a small template region, not a single block.

Minimal shapes:

```text
if (cond) then_body;

  [expr-eval] -> [if-head] ->true-> [if-then] -> [if-join]
                         \->false--------------> [if-join]
```

```text
if (cond) then_body; else else_body;

  [expr-eval] -> [if-head] ->true->  [if-then] -> [if-join]
                         \->false-> [if-else] -> [if-join]
```

## Template Block Kinds

These kinds are defined in [e/e_templates.h](/home/nyhl/workspace/elara.cpp/libElaraParallelAssembly/e/e_templates.h).

- `E_TPL_EXPR_EVAL`
  Evaluates the condition expression and produces a canonical boolean result.
- `E_TPL_IF_HEAD`
  Consumes the predicate and selects the true/false control-flow edge.
- `E_TPL_IF_THEN`
  Holds the lowered body for the `then` branch.
- `E_TPL_IF_ELSE`
  Holds the lowered body for the `else` branch.
- `E_TPL_IF_JOIN`
  Re-convergence point after either branch.
- `E_TPL_RETURN`
  Terminal block for paths that return.
- `E_TPL_SEQ`
  Generic sequential block for non-branch statements.

## Predicate Contract

For the initial lowering pass:

- `0` means false
- non-zero means true
- the condition is evaluated before `if-head`
- `if-head` only consumes the already-computed predicate

This intentionally separates:

- expression lowering
- control-flow shaping

That separation matters because variable addressing and temporary lifetime are
not solved yet.

## EPA Mapping

The intended EPA shape is:

1. Lower the condition into EPA instructions that leave a boolean result.
2. Emit an `if-head` block that branches using EPA jump instructions.
3. Emit `then` and `else` blocks as ordinary EPA statement blocks.
4. Emit an explicit join label/block unless both branches are terminal.

Conceptually:

```text
L_cond:
  ... evaluate condition ...
  JZ L_else_or_join
  JMP L_then

L_then:
  ... lowered then body ...
  JMP L_join

L_else:
  ... lowered else body ...
  JMP L_join

L_join:
  ... continue ...
```

The exact final emission may remove redundant jumps, but the template system
should keep the region shape explicit even if the code generator later folds it.

## Design Constraints

- `if-head` should not also contain body instructions.
  This keeps branch selection isolated and easier to lint.
- `if-join` should exist in the template graph even if it later optimizes away.
  This makes nested lowering and diagnostics much simpler.
- Branch blocks may terminate early with `return`.
  In that case the outgoing edge becomes terminal and the join can be omitted
  during final EPA emission.
- An `if` without `else` still has a false edge.
  That false edge targets the join block directly.

## Why A Region Instead Of One Block

Because later passes will need:

- validator pre-entry insertion
- worker-safe branch diagnostics
- static cross-checks between compile-time regions and runtime behavior
- optional edge instrumentation without rewriting expression lowering

A region model preserves that structure.

## First Lowering ABI

The first lowering ABI for `if` should be:

- input: a canonical boolean produced by prior expression lowering
- output:
  - true edge enters `then`
  - false edge enters `else` or `join`
- no phi/value merge yet
- no variable materialization yet

This is enough to get branch structure correct before solving storage.

## Suggested Follow-On Work

1. Extend the `E` AST with `if` and optional `else`.
2. Build a template graph from statement trees.
3. Add a graph dump mode beside the current semantic manifest.
4. Emit skeleton EPA labels/jumps from the template graph.
5. Only after that, add variable references and value transport across joins.
