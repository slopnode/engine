@page vis (.vis) Potentionally visible set

Magic 0x31535650 (PVS1), version 1.

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x31535650 |
| version | u32 | 1 |
| leafCount | u32 | Matches BSP leaf count |
| wordsPerRow | u32 | `(leafCount + 31) / 32` |
| bits[leafCount * wordsPerRow] | u32 | Row-major; bit `(to & 31)` in word `from * wordsPerRow + (to >> 5)` |