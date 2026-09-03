# Function Map

`docs/function_map.csv` is the machine-readable progress ledger for discovered PS2 functions.

Columns:

- `PS2 Address`
- `Size`
- `Provisional Name`
- `Status`
- `Native Function`
- `Confidence`
- `Notes`

Allowed function status values:

- `UNKNOWN`
- `DISCOVERED`
- `ANALYZED`
- `STUBBED`
- `IMPLEMENTED`
- `VERIFIED`

A provisional name is an analyst label, not proof of semantics. Address, call references, memory references, confidence and validation notes must be preserved as analysis progresses.
