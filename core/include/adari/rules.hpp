// SPDX-License-Identifier: Apache-2.0
// Rule templates, rule files and the evaluator.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "adari/map.hpp"

namespace adari {

enum class Severity { kError, kWarning, kNote };

const char* to_string(Severity s);

// One rule instance from a rule file. Which fields a template reads is listed
// in docs/rules.md; unused fields are ignored.
struct Rule {
    std::string id;
    std::string tmpl;  // template name, e.g. "reachable"
    Severity severity = Severity::kError;
    std::string description;

    std::string from;     // selector
    std::string to;       // selector
    std::string target;   // selector
    std::string door;     // selector
    std::string subject;  // selector
    std::string object;   // selector
    std::string region;   // selector
    std::string blocker;  // selector, matched against variant options and regions

    double radius = 0.0;        // agent radius, world units
    double min = -1.0;          // path length lower bound, world units (-1 = none)
    double max = -1.0;          // path length upper bound, world units (-1 = none)
    double distance = 0.0;      // for "near", world units
    std::size_t min_cells = 1;  // for "no_isolated_walkable"
    std::vector<DoorOverride> doors;
};

struct Options {
    std::size_t max_combinations = 4096;  // enumerate up to this many variants
    std::size_t sample_count = 512;       // otherwise sample this many
    std::uint32_t sample_seed = 1;
    std::size_t sight_samples = 256;      // per cell set in visibility tests
};

struct RuleSet {
    std::vector<Rule> rules;
    Options options;
    // From the rule file's "reader": {"meanings": {...}}; passed to the map
    // reader so a project can say what its tile classes or IntGrid values mean.
    std::map<std::string, std::string> meanings;
};

// Parses a rule file (JSON). Throws std::runtime_error with a readable
// message on malformed input or an unknown template.
RuleSet parse_rules(const std::string& json_text);
RuleSet load_rules(const std::string& path);

// The template names the evaluator knows.
const std::vector<std::string>& templates();

struct Finding {
    std::string rule_id;
    std::string tmpl;
    Severity severity = Severity::kError;
    std::string message;
    std::vector<int> cells;  // cells to highlight (region, unreachable target, overlap)
    std::vector<int> path;   // counterexample path or sight line, in order
    Choice variant;          // the failing variant; empty when the map has no groups
    std::string variant_text;
    double measured = 0.0;
    double required = 0.0;
    bool has_measure = false;
};

enum class Status { kPassed, kFailed, kNotChecked };

const char* to_string(Status s);

struct RuleResult {
    std::string rule_id;
    std::string tmpl;
    Severity severity = Severity::kError;
    Status status = Status::kNotChecked;
    // How the variants were covered: "single map", "bound", "every variant",
    // "each option", "sampled".
    std::string method;
    std::size_t variants_checked = 0;
    std::size_t variants_total = 1;
    std::size_t variants_failed = 0;
    std::string note;
};

struct Report {
    std::string map_name;
    std::string map_file;
    std::vector<RuleResult> results;
    std::vector<Finding> findings;
    std::size_t count(Severity s) const;
    bool failed() const;  // any error-level finding or not-checked error rule
};

Report evaluate(const Map& map, const RuleSet& rules);

// Human-readable description of a variant, e.g. "crates=b, barrels=none".
std::string describe(const Map& map, const Choice& choice);

}  // namespace adari
