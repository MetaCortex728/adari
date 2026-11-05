// SPDX-License-Identifier: Apache-2.0
// adari: checks a map against a rule file.
//
//   adari check MAP --rules RULES [--level NAME | --all-levels] [--meaning NAME=MEANING]...
//                   [--json FILE] [--sarif FILE] [--overlay FILE.png] [--scale N] [--quiet]
//   adari inspect MAP [--level NAME] [--meaning NAME=MEANING]...
//   adari templates
//   adari version
//
// Exit codes: 0 all error-level rules hold, 1 an error-level rule failed or
// could not be checked, 2 bad arguments or unreadable input.
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "adari/readers.hpp"
#include "adari/report.hpp"
#include "adari/rules.hpp"

namespace {

int usage(const char* msg = nullptr) {
    if (msg) std::cerr << "adari: " << msg << "\n\n";
    std::cerr << "usage:\n"
                 "  adari check MAP --rules RULES [--level NAME | --all-levels] [--meaning NAME=MEANING]...\n"
                 "                  [--json FILE] [--sarif FILE] [--overlay FILE.png] [--scale N] [--quiet]\n"
                 "  adari inspect MAP [--level NAME] [--meaning NAME=MEANING]...\n"
                 "  adari templates\n"
                 "  adari version\n\n"
                 "MAP: ASCII (.txt), Adari JSON, Tiled (.tmx, .tmj, or JSON), LDtk (.ldtk)\n"
                 "MEANING: wall, low (blocks walking only), sight (blocks sight only), floor;\n"
                 "         NAME is an LDtk IntGrid value identifier or a Tiled tile class\n";
    return 2;
}

bool is_ldtk(const std::string& path) {
    const auto dot = path.find_last_of('.');
    return dot != std::string::npos && path.substr(dot) == ".ldtk";
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Adds "-LEVEL" before the extension: out.json -> out-Level_1.json.
std::string with_level(const std::string& path, const std::string& level) {
    if (path.empty() || level.empty()) return path;
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path + "-" + level;
    return path.substr(0, dot) + "-" + level + path.substr(dot);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return usage();
    const std::string cmd = args[0];
    if (cmd == "version" || cmd == "--version") {
        std::cout << "adari " << adari::version() << "\n";
        return 0;
    }
    if (cmd == "templates") {
        for (const auto& t : adari::templates()) std::cout << t << "\n";
        return 0;
    }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        usage();
        return 0;
    }
    if (cmd != "check" && cmd != "inspect") return usage(("unknown command '" + cmd + "'").c_str());

    std::string map_path, rules_path, json_out, sarif_out, overlay_out;
    adari::ReaderOptions reader;
    bool all_levels = false;
    int scale = 0;
    bool quiet = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= args.size()) return false;
            dst = args[++i];
            return true;
        };
        if (a == "--rules") { if (!value(rules_path)) return usage("--rules needs a file"); }
        else if (a == "--level") { if (!value(reader.level)) return usage("--level needs a name"); }
        else if (a == "--all-levels") all_levels = true;
        else if (a == "--meaning") {
            std::string m;
            if (!value(m) || m.find('=') == std::string::npos) return usage("--meaning needs NAME=MEANING");
            reader.meanings[m.substr(0, m.find('='))] = m.substr(m.find('=') + 1);
        }
        else if (a == "--json") { if (!value(json_out)) return usage("--json needs a file"); }
        else if (a == "--sarif") { if (!value(sarif_out)) return usage("--sarif needs a file"); }
        else if (a == "--overlay") { if (!value(overlay_out)) return usage("--overlay needs a file"); }
        else if (a == "--scale") {
            std::string s;
            if (!value(s)) return usage("--scale needs a number");
            try { scale = std::stoi(s); } catch (...) { return usage("--scale needs a number"); }
        }
        else if (a == "--quiet") quiet = true;
        else if (!a.empty() && a[0] == '-') return usage(("unknown option '" + a + "'").c_str());
        else if (map_path.empty()) map_path = a;
        else return usage("only one map per run");
    }
    if (map_path.empty()) return usage("missing MAP");
    if (cmd == "check" && rules_path.empty()) return usage("missing --rules");
    if (all_levels && !reader.level.empty()) return usage("use --level or --all-levels, not both");

    try {
        std::vector<std::string> levels = {reader.level};
        if (is_ldtk(map_path) && (all_levels || cmd == "inspect")) {
            const auto names = adari::ldtk_levels(slurp(map_path));
            if (cmd == "inspect" && reader.level.empty() && names.size() > 1) {
                std::cout << "levels:";
                for (const auto& n : names) std::cout << " " << n;
                std::cout << "  (showing the first; pick one with --level)\n";
            }
            if (all_levels) levels = names;
        }
        if (cmd == "inspect") {
            std::cout << adari::summarize(adari::load_map(map_path, reader));
            return 0;
        }

        adari::RuleSet rules = adari::load_rules(rules_path);
        for (const auto& [k, v] : rules.meanings)
            if (!reader.meanings.count(k)) reader.meanings[k] = v;  // the command line wins
        bool failed = false;
        for (const std::string& level : levels) {
            adari::ReaderOptions opt = reader;
            opt.level = level;
            const adari::Map map = adari::load_map(map_path, opt);
            const adari::Report rep = adari::evaluate(map, rules);
            const std::string tag = all_levels ? level : std::string();
            if (!quiet) std::cout << adari::to_text(map, rep) << (levels.size() > 1 ? "\n" : "");
            if (!json_out.empty()) adari::write_file(with_level(json_out, tag), adari::to_json(map, rep));
            if (!sarif_out.empty()) adari::write_file(with_level(sarif_out, tag), adari::to_sarif(map, rep));
            if (!overlay_out.empty())
                adari::write_file(with_level(overlay_out, tag), adari::overlay_png(map, rep, scale));
            failed = failed || rep.failed();
        }
        return failed ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "adari: " << e.what() << "\n";
        return 2;
    }
}
