#include "choppage/Preset.h"
#include "choppage/Params.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bc::Preset
{
    namespace fs = std::filesystem;

    std::string make (const std::string& name, const float* p, const std::string& session)
    {
        std::ostringstream o;
        o << "CHOPPAGE PRESET 1\n";
        o << "name " << name << "\n";
        o << "[params]\n";
        const auto& defs = paramDefs();
        for (int i = 0; i < (int) defs.size() && i < kNumCoreParams; ++i) o << defs[(size_t) i].id << " " << p[i] << "\n";
        o << "[session]\n" << session;
        return o.str();
    }

    bool parse (const std::string& text, Parsed& out)
    {
        if (text.rfind ("CHOPPAGE PRESET", 0) != 0) return false;
        out = {};
        std::istringstream ss (text);
        std::string line;
        enum { head, params, session } part = head;
        while (std::getline (ss, line))
        {
            if (part == session) { out.session += line + "\n"; continue; }
            if (line == "[params]") { part = params; continue; }
            if (line == "[session]") { part = session; continue; }
            if (part == head && line.rfind ("name ", 0) == 0) out.name = line.substr (5);
            else if (part == params)
            {
                const auto sp = line.find (' ');
                if (sp != std::string::npos) out.params[line.substr (0, sp)] = (float) std::atof (line.c_str() + sp + 1);
            }
        }
        return true;
    }

    std::vector<std::pair<std::string, std::string>> list (const std::string& folder)
    {
        std::vector<std::pair<std::string, std::string>> v;
        std::error_code ec;
        for (fs::directory_iterator it (folder, ec), end; ! ec && it != end; it.increment (ec))
        {
            if (! it->is_regular_file (ec) || it->path().extension() != kExtension) continue;
            std::string text, name = it->path().stem().string();
            Parsed p;
            if (readFile (it->path().string(), text) && parse (text, p) && ! p.name.empty()) name = p.name;
            v.emplace_back (name, it->path().string());
        }
        std::sort (v.begin(), v.end(), [] (const auto& a, const auto& b)
        {
            std::string x = a.first, y = b.first;
            for (auto& c : x) c = (char) std::toupper ((unsigned char) c);
            for (auto& c : y) c = (char) std::toupper ((unsigned char) c);
            return x < y;
        });
        return v;
    }

    std::string freePath (const std::string& folder, const std::string& name)
    {
        std::string safe;
        for (char c : name) safe += (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') ? '-' : c;
        if (safe.empty()) safe = "CHOPPAGE";
        std::error_code ec;
        for (int k = 1; k < 1000; ++k)
        {
            const auto p = fs::path (folder) / (safe + (k > 1 ? " " + std::to_string (k) : std::string()) + kExtension);
            if (! fs::exists (p, ec)) return p.string();
        }
        return (fs::path (folder) / (safe + kExtension)).string();
    }

    bool readFile (const std::string& path, std::string& text)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f) return false;
        std::ostringstream s;
        s << f.rdbuf();
        text = s.str();
        return true;
    }

    bool writeFile (const std::string& path, const std::string& text)
    {
        std::error_code ec;
        fs::create_directories (fs::path (path).parent_path(), ec);
        std::ofstream f (path, std::ios::binary);
        if (! f) return false;
        f << text;
        return (bool) f;
    }
}
