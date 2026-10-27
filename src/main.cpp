/// @file main.cpp
/// @brief The command-line entry point: options, script mode, interactive mode, server mode.
///
///     ./myTinySql --sqlfile=examples/select.sql   run a script
///     ./myTinySql                                 type statements (or pipe them in)
///     ./myTinySql --http                          browser page on http://localhost:8080

#include <unistd.h>

#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Output.hpp"
#include "Server.hpp"
#include "Session.hpp"

namespace {

using namespace tinysql;

/// @brief The text printed by `--help` and after a wrong option.
constexpr std::string_view kUsage = R"(Usage: myTinySql [options]

  --sqlfile=FILE   run the statements in FILE ('-' for standard input)
  --http           start the browser server (default port 8080)
  --port=N         listen on port N instead of 8080
  --allow=LIST     comma-separated client IP prefixes the server accepts,
                   e.g. 127.,192.168. (default: everyone)
  --data=DIR       directory with SYSTEM_TABLE and the *_DATA files (default: data)
  --web=DIR        directory with the browser page's files (default: web)
  --help           show this help

With no --sqlfile and no --http, statements are read from standard input.
)";

/// @brief The command-line options, after parsing.
struct Options {
    std::optional<std::string> sqlFile;             ///< `--sqlfile`: the script to run (`-` for standard input).
    bool http = false;                              ///< `--http`: start the browser server.
    std::optional<std::uint16_t> port;              ///< `--port`: the server port, if not the default.
    std::optional<std::vector<std::string>> allow;  ///< `--allow`: client IP prefixes, if given.
    std::filesystem::path dataDir = "data";         ///< `--data`: the directory holding the tables.
    std::filesystem::path webDir = "web";           ///< `--web`: the directory holding the browser page.
};

/// @brief Splits a comma-separated list.
/// @param text E.g. "127.,192.168.".
/// @return The non-empty items.
std::vector<std::string> splitList(std::string_view text)
{
    std::vector<std::string> items;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        if (std::string_view item = text.substr(0, comma); !item.empty()) {
            items.emplace_back(item);
        }
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return items;
}

/// @brief Parses the command line. Options take their value as `--name=value` or `--name value`.
/// @param argc Number of arguments, as given to main().
/// @param argv The arguments, as given to main().
/// @return The options.
/// @throws std::invalid_argument On an unknown option, a missing or bad value, or `--sqlfile` together with `--http`.
Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        std::string_view name = arg;
        std::optional<std::string_view> value;
        if (const std::size_t eq = arg.find('='); eq != std::string_view::npos) {
            name = arg.substr(0, eq);
            value = arg.substr(eq + 1);
        }
        auto requireValue = [&]() -> std::string_view {
            if (value) {
                return *value;
            }
            if (i + 1 >= argc) {
                throw std::invalid_argument(std::format("{} needs a value", name));
            }
            return argv[++i];
        };

        if (name == "--help" || name == "-h") {
            std::cout << kUsage;
            std::exit(0);
        } else if (name == "--sqlfile") {
            options.sqlFile = std::string(requireValue());
        } else if (name == "--http") {
            options.http = true;
        } else if (name == "--port") {
            const std::string_view text = requireValue();
            std::uint16_t port = 0;
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), port);
            if (ec != std::errc{} || ptr != text.data() + text.size() || port == 0) {
                throw std::invalid_argument(std::format("invalid port '{}'", text));
            }
            options.port = port;
        } else if (name == "--allow") {
            options.allow = splitList(requireValue());
        } else if (name == "--data") {
            options.dataDir = std::string(requireValue());
        } else if (name == "--web") {
            options.webDir = std::string(requireValue());
        } else {
            throw std::invalid_argument(std::format("unknown option '{}'", arg));
        }
    }
    if (options.sqlFile && options.http) {
        throw std::invalid_argument("choose either --sqlfile or --http, not both");
    }
    return options;
}

/// @brief Runs a whole script and prints every result.
/// @param session The session to run it in.
/// @param in Where to read the script from (a file or standard input).
/// @return The exit code: 0 if every statement succeeded, 1 otherwise.
int runScript(Session& session, std::istream& in)
{
    std::stringstream script;
    script << in.rdbuf();
    int exitCode = 0;
    for (const StatementResult& result : session.executeScript(script.str())) {
        std::cout << renderText(result);
        if (result.failed) {
            exitCode = 1;
        }
    }
    return exitCode;
}

/// @brief Reads statements at a `tinysql>` prompt until end of input (Ctrl+D).
/// @param session The session to run them in.
/// @return The exit code, always 0.
int runInteractive(Session& session)
{
    std::string line;
    while (std::cout << "tinysql> " << std::flush, std::getline(std::cin, line)) {
        for (const StatementResult& result : session.executeScript(line)) {
            std::cout << renderText(result);
        }
    }
    std::cout << '\n';
    return 0;
}

}  // namespace

/// @brief Program entry point.
/// @param argc Number of command-line arguments.
/// @param argv The command-line arguments.
/// @return 0 on success; 1 if a script statement failed or an error occurred; 2 for a usage error
///         or a missing file or data directory.
int main(int argc, char** argv)
{
    try {
        const Options options = parseOptions(argc, argv);
        if (!std::filesystem::is_directory(options.dataDir)) {
            std::cerr << std::format("data directory '{}' not found (use --data=DIR)\n", options.dataDir.string());
            return 2;
        }

        if (options.http) {
            if (!std::filesystem::is_regular_file(options.webDir / "index.html")) {
                std::cerr << std::format("web page '{}' not found (use --web=DIR)\n",
                                         (options.webDir / "index.html").string());
                return 2;
            }
            ServerConfig config{
                .port = options.port.value_or(8080),
                .dataDir = options.dataDir,
                .webDir = options.webDir,
                .allowedClients = options.allow.value_or(std::vector<std::string>{}),
            };
            runServer(config);
            return 0;
        }

        Session session(options.dataDir);
        if (!options.sqlFile || *options.sqlFile == "-") {
            return ::isatty(STDIN_FILENO) && !options.sqlFile ? runInteractive(session) : runScript(session, std::cin);
        }
        std::ifstream file(*options.sqlFile);
        if (!file) {
            std::cerr << std::format("cannot open '{}'\n", *options.sqlFile);
            return 2;
        }
        return runScript(session, file);
    } catch (const std::invalid_argument& e) {
        std::cerr << "myTinySql: " << e.what() << "\n\n" << kUsage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "myTinySql: " << e.what() << '\n';
        return 1;
    }
}
