# Plan: <objective>

**Row:** <state-file row this plan serves> · **Ticket:** <key or "to create"> · **State:** draft
**Requested by / date:** <who, when, their words>

## 1. Hypothesis
We believe **<change>** will produce **<effect>** because **<the component, path, table or field it
touches>**.

**Falsifiers** (observations that would refute this; at least two):
- <observation> → <what it would mean, and what we do instead>
- <observation> → <...>

## 2. Feasibility, derived from the row
| Question | Answer | Evidence |
|---|---|---|
| Do we know WHERE to change? | yes / partly / no | <file, config, table, address> |
| Is every requirement MAPPED? | yes / missing: <ids> | <row's requires and missing columns> |
| Is there a TOOL to apply and observe it? | yes / adapt <x> / none | <script, test, env, probe> |

**Level:** HIGH / MEDIUM / LOW / BLOCKED. <one line why>

## 3. Options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A | | | chosen: <reason> |
| B | | | rejected: <reason> |

## 4. Edge cases
- <what can go wrong> → <how it is handled>

## 5. Acceptance criteria (observations)
- AC1: `<command>` prints `<expected>`; or: <live check and what it shows>
- AC2: ...

## 6. Where to change
- `<path>`: <what>
- Surface costs the owner decides: <new env var / gate / hook / dependency / none>

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | <single-target first> | | |
| 2 | <broad rollout> | | |

## 8. Cost, risk, rollback
- Cost: <sessions, runs, money>
- Risk: <what can break, who is affected>
- Rollback: `<one-line revert>`

## 9. Docs to update
| Doc | Change |
|---|---|

## 10. Outcome (filled by /update-map when finished)
- What happened:
- What the falsifiers said:
- What changed in the state file:
