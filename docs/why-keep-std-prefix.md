# Why the code keeps writing `std::`

Question: in a small project, is it worth avoiding the repeated `std::` with `using namespace std;`?

Answer: **keep `std::`**, even in a small project.

## 1. Never in a header: this one is a firm rule

`using namespace std;` in a `.hpp` file is copied into every file that includes it. The 2023 `myToolBox.h` (now in `legacy/`) did exactly this, so every file in the old project got all of `std` whether it wanted it or not.

## 2. In a `.cpp` file it's allowed, but it can bite

`std` has hundreds of names, and some are ordinary words: `count`, `find`, `distance`, `size`, `data`, `move`, `format`, `byte`. Suppose you write your own function `count()` or `format()`, or a variable called `distance`. With `using namespace std;`, the compiler may pick the wrong one or report an "ambiguous" error. These errors are confusing, and they can appear later, when a new compiler version adds names to `std`.

## 3. `std::` tells the reader where things come from

In `Session.cpp` you see `std::optional`, `std::visit` and `parseStatement()`. The first two are clearly from the standard library, and the third is clearly ours. For a learning project this helps, because you can immediately tell which names you can look up on cppreference.com.

## 4. If something is long or repeated, there are targeted tools

These remove the noise without importing all of `std`:

```cpp
using std::string;                          // just this one name, in a .cpp file
namespace fs = std::filesystem;             // a short alias for a long namespace
using Row = std::vector<std::string>;       // a name for a type you use a lot
```

The project already uses the third form: `Row` in `TableFile.hpp` and `Statement` in `Statement.hpp`. That kind of alias also adds meaning, because "a Row" says more than "a vector of strings".

## In short

`std::` costs five characters and avoids a whole class of strange errors, and professional C++ code almost always keeps it.
