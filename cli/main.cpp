// SPDX-License-Identifier: Apache-2.0
// adari: checks a map against a rule file.
//
//   adari check MAP --rules RULES [--level NAME] [--json FILE] [--sarif FILE]
//                   [--overlay FILE.png] [--scale N] [--quiet]
//   adari templates
//   adari version
//
// Exit codes: 0 all error-level rules hold, 1 an error-level rule failed or
// could not be checked, 2 bad arguments or unreadable input.
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "adari/readers.hpp"
#include "adari/report.hpp"
#include "adari/rules.hpp"

namespace {

int usage(const char* msg = nullptr) {
    if (msg) std::cerr << "adari: " << msg << "\n\n";
    std::cerr << "usage:\n"
                 "  adari check MAP --rules RULES [--level NAME] [--json FILE] [--sarif FILE]\n"
                 "                  [--overlay FILE.png] [--scale N] [--quiet]\n"
                 "  adari templates\n"
                 "  adari version\n\n"
                 "MAP: ASCII map (.txt), Adari JSON, Tiled (.tmj or JSON), LDtk (.ldtk)\n";
    return 2;
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
    if (cmd != "check") return usage(("unknown command '" + cmd + "'").c_str());

    std::string map_path, rules_path, level, json_out, sarif_out, overlay_out;
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
        else if (a == "--level") { if (!value(level)) return usage("--level needs a name"); }
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
    if (rules_path.empty()) return usage("missing --rules");

    try {
        const adari::Map map = adari::load_map(map_path, level);
        const adari::RuleSet rules = adari::load_rules(rules_path);
        const adari::Report rep = adari::evaluate(map, rules);
        if (!quiet) std::cout << adari::to_text(map, rep);
        if (!json_out.empty()) adari::write_file(json_out, adari::to_json(map, rep));
        if (!sarif_out.empty()) adari::write_file(sarif_out, adari::to_sarif(map, rep));
        if (!overlay_out.empty()) adari::write_file(overlay_out, adari::overlay_png(map, rep, scale));
        return rep.failed() ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "adari: " << e.what() << "\n";
        return 2;
    }
}
