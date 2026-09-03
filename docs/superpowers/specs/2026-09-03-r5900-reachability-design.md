# R5900 Reachability Analysis — Design

## Scope

Discover code reachable from one known R5900 entry point using only explicit control-flow evidence produced by the validated basic-block analyzer. Do not execute guest code, propagate registers, infer indirect targets, or claim function boundaries.

## Traversal policy

Follow only edges whose target is explicit and represents same-flow continuation:

- conditional branch taken/not-taken;
- direct jump;
- call continuation;
- instruction-limit fallthrough.

Direct call targets are recorded as call evidence but are not traversed as part of the current reachability graph. Indirect calls and indirect jumps remain unresolved evidence.

## Output

- deterministic list of discovered basic blocks;
- direct/indirect call sites;
- issues for unresolved exits, target-analysis failures, block-limit truncation, and leaders that land inside another discovered block or delay slot.

## Error policy

Failure to analyze the requested entry point is fatal. Failure to analyze a later explicit successor is recorded as an issue and does not discard the rest of the graph.

## Non-goals

- no canonical block splitting yet;
- no function discovery;
- no following call targets;
- no indirect-target recovery;
- no execution/emulation;
- no symbol inference.
