// The HELIX page layout, shared by the desktop editor and the MPC skin generator. Pure data.
// Sections hold up to four parameters, so each one maps onto one Q-Link block of four on the MPC.
#pragma once
#include <string>
#include <vector>

namespace helix {

struct UiSection
{
    std::string title;
    std::vector<int> params;    // ParamId values, at most 4; -1 = empty slot
    std::string group;          // MPC page this block goes on (<= 8 chars); "" = fill pages in order
    UiSection() = default;
    UiSection(std::string t, std::vector<int> p, std::string g = {}) : title(std::move(t)), params(std::move(p)), group(std::move(g)) {}
};

struct UiPage
{
    std::string name;           // OSC A | OSC B | SUB/NOISE | FILTERS | ENV | LFO/PERF | MATRIX | FX | MACROS | BROWSER
    std::vector<UiSection> sections;
};

const std::vector<UiPage>& uiPages();

} // namespace helix
