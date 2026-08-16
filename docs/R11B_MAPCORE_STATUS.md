# Shadow64 R11B map core

R11B keeps the R11 real-map runtime and carries forward the R11A GRP validation/recovery fixes.
It additionally fixes the Phase 0 `.gitignore` collision that stopped R11A after successful map extraction:

- `filesystem/sw_first.map` is force-staged explicitly because it is generated from the user's local SW.GRP;
- the script verifies that exact map is present in the staged commit before pushing;
- all source/docs/tools continue to obey normal Git ignore rules;
- the downloaded GitHub Actions artifact remains bound to the exact pushed commit.

Runtime milestone remains: load and walk `$bullet.map` (Seppuku Station) with Build sector/wall parsing, portals, collision/sliding, first-person wireframe view, and overhead map.
