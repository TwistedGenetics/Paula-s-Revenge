// Paula's Revenge - no AAC decoder on this system (Linux builds: only the tests run there)
#include "../RadioFormats.h"

namespace prv::radio
{
std::unique_ptr<AacBackend> makeAacBackend() { return nullptr; }
bool aacAvailable() { return false; }
} // namespace prv::radio
