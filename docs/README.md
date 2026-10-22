# myTinySQL documentation

Notes for studying the project. Read them in this order:

| File | What it covers |
| --- | --- |
| [project-guide.md](project-guide.md) | What myTinySQL is, the folder layout, how a query flows through the code, the file formats, building, usage, and what changed from 2023 |
| [old-vs-new.md](old-vs-new.md) | Where each piece of the 2023 code went, the 10 main ideas that changed, and a suggested reading order for the code in `include/` and `src/` |
| [statement-hpp-explained.md](statement-hpp-explained.md) | `include/Statement.hpp` line by line, with the same declarations written in plain C for comparison |
| [why-keep-std-prefix.md](why-keep-std-prefix.md) | Why the code writes `std::` everywhere instead of `using namespace std;` |

The online version of the project guide (with the drawn diagram and comments) is the
"myTinySQL — Project Guide" doc on claude.ai. This folder holds a copy as of 2026-10-21.
