# Installation

How to install, build and run myTinySQL. It takes a few minutes on a Linux machine with a recent C++ compiler.

## Requirements

| What | Needed | Notes |
| --- | --- | --- |
| Operating system | **Linux** | Tested on Ubuntu 26.04 LTS (x86-64). Other distributions with a recent compiler should work. |
| C++ compiler | **g++ 13 or newer** (C++20) | The code uses `std::format`, which GCC provides from version 13. Tested with g++ 15.2. |
| Build tool | **GNU make** | Tested with GNU Make 4.4. |
| Libraries | **none to install** | Only the C++ standard library and the Linux system headers (`sys/socket.h`, `unistd.h`, ...), which come with the compiler. |
| Git | to download the code | Or download the zip from GitHub instead. |
| Doxygen | optional | Only for `make doxygen` (the HTML code documentation). Tested with 1.15. |
| Web browser | optional | For the `--http` mode, e.g. Chromium or Firefox. |

**Not supported:** macOS (the server uses `MSG_NOSIGNAL`, which exists only on Linux) and native Windows (no POSIX sockets). On Windows, WSL with Ubuntu should work, but it has not been tested.

## 1. Install the tools

**Ubuntu 24.04 or newer, Debian 13 or newer:**

```bash
sudo apt install build-essential git
```

**Fedora 39 or newer:**

```bash
sudo dnf install gcc-c++ make git
```

Optional, for the HTML code documentation:

```bash
sudo apt install doxygen
```

Check the compiler version: the first line must say 13 or higher.

```bash
g++ --version
```

On older systems whose default compiler is older (for example Ubuntu 22.04 with g++ 11, or Debian 12 with g++ 12), install g++ 13 or newer from your distribution, then build with `make CXX=g++-13` (use the version you installed).

## 2. Get the code

```bash
git clone https://github.com/ricard1406/myTinySql.git
cd myTinySql
```

Without git: on the [GitHub page](https://github.com/ricard1406/myTinySql) click **Code → Download ZIP**, unzip it, and open a terminal in that folder.

## 3. Build

```bash
make
```

This creates the program `./myTinySql`. Object files go to `build/`.

## 4. Check that it works

```bash
make test
```

Seven test scripts run on a copy of the sample tables. Each line must say `PASS`.

## 5. Run it

Always run the program from the project folder: it reads its tables from `data/` in the current folder (or use `--data=DIR`).

**Run a SQL script:**

```bash
./myTinySql --sqlfile=examples/select.sql
```

**Type statements at a prompt** (Ctrl+D to quit):

```bash
./myTinySql
```

**Use it from a browser:**

```bash
./myTinySql --http
```

Then open http://localhost:8080. The page lists the tables on the left; you can search, add, edit and delete rows, create new tables, and run any SQL in the console at the bottom. Stop the server with Ctrl+C.

All options:

```bash
./myTinySql --help
```

## 6. Optional: the code documentation

```bash
make doxygen
```

Then open `build/doxygen/html/index.html` in a browser. The same pages are online at https://ricard1406.github.io/myTinySql/.

## Troubleshooting

| Problem | Cause and fix |
| --- | --- |
| `fatal error: format: No such file or directory` while building | The compiler is too old. Install g++ 13 or newer and build with `make CXX=g++-13`. |
| ``data directory 'data' not found`` | The program was started outside the project folder. `cd` into it, or pass `--data=/path/to/myTinySql/data`. |
| ``web page 'web/index.html' not found`` | The program was started outside the project folder, so the page's files in `web/` were not found. `cd` into it, or pass `--web=/path/to/myTinySql/web`. |
| `bind to port 8080: Address already in use` | Another program (or another myTinySql) already uses port 8080. Stop it, or choose another port: `./myTinySql --http --port=8081`. |
| The browser page does not load from another computer | The server listens on all network interfaces, but a firewall may block port 8080. Use `--allow=192.168.` to accept only your home network. |

## Uninstall

Nothing is installed outside the project folder: to remove myTinySQL, delete the folder.
