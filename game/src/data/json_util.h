#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace opente::data {

/// Normalizes a field that the extractor may emit as `null`, a single
/// string, or a list of strings (e.g. `guards.json`'s `required_weapon`,
/// which is `null`/a 4-letter id/a pair of ids depending on the source
/// record's field width) into a `vector<string>`.
inline std::vector<std::string> json_to_string_list(const nlohmann::json& j) {
    if (j.is_null()) {
        return {};
    }
    if (j.is_string()) {
        return {j.get<std::string>()};
    }
    if (j.is_array()) {
        return j.get<std::vector<std::string>>();
    }
    return {};
}

/// Normalizes a field that may be a 4-letter tag (string) or a raw int
/// (e.g. `buildings.json`'s `category`/`type`, which come from a `bldg.defa`
/// field whose flag -- and therefore decoded type -- varies by record) into
/// a string, stringifying numbers and mapping `null` to "".
inline std::string json_to_string_any(const nlohmann::json& j) {
    if (j.is_null()) {
        return "";
    }
    if (j.is_string()) {
        return j.get<std::string>();
    }
    if (j.is_number_integer()) {
        return std::to_string(j.get<long long>());
    }
    return j.dump();
}

/// Normalizes an array field whose elements may themselves be a string, an
/// int, `null`, or a nested `[a, b]` pair (e.g. `technologies.json`'s
/// `unlocks.buildings`/`unlocks.transporters`/`unlocks.pathways`, where a
/// pair represents "either of these two unlocks the same thing" -- see
/// gotcha 7 re: `0x40` reference-pair fields). Each element is converted via
/// `json_to_string_any`, so a pair becomes its JSON-array string form (e.g.
/// `"[\"gunp\",\"salp\"]"`). Returns `{}` if `j` is missing/null/not an
/// array.
inline std::vector<std::string> json_to_string_list_any(const nlohmann::json& j) {
    if (!j.is_array()) {
        return {};
    }
    std::vector<std::string> result;
    result.reserve(j.size());
    for (const auto& element : j) {
        result.push_back(json_to_string_any(element));
    }
    return result;
}

/// Like `j.value(key, default_value)`, but also falls back to
/// `default_value` when the field is present but `null` -- several string
/// fields in the extracted tables (e.g. `commodities.json`'s `name`,
/// `guards.json`'s `produced_by_building`, `events.json`'s `name`) can be
/// `null` for records with no decoded name. `j.value()` alone throws
/// `type_error.302` in that case since it only substitutes the default for
/// *missing* keys, not present-but-null ones.
inline std::string json_string_or(const nlohmann::json& j, const std::string& key, std::string default_value) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return default_value;
    }
    return it->get<std::string>();
}

}  // namespace opente::data
