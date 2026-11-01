// Paula's Revenge - STEMS without ONNX Runtime (a build that leaves it out)
#include "Stems.h"

namespace prv::stems
{
std::unique_ptr<DemucsModel> loadOnnxModel (const std::string&, int, std::string& error)
{
    error = "STEMS AREN'T BUILT INTO THIS VERSION";
    return nullptr;
}
bool onnxAvailable() { return false; }
} // namespace prv::stems
