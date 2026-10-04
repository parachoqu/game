#include "core/data/DataHash.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "core/Paths.h"
#include "core/Sha256.h"

namespace rpg {

std::string dataHash(const std::filesystem::path& dir) {
  std::vector<std::pair<std::string, std::filesystem::path>> files;
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    std::string rel = pathToUtf8(std::filesystem::relative(it->path(), dir, ec));
    std::replace(rel.begin(), rel.end(), '\\', '/');
    files.emplace_back(std::move(rel), it->path());
  }
  std::sort(files.begin(), files.end());
  Sha256 h;
  for (const auto& [rel, path] : files) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::erase(bytes, '\r');  // checkout no Windows com CRLF: o mesmo resumo
    h.update(rel);
    h.update(std::string_view("\0", 1));
    h.update(std::string_view(bytes.data(), bytes.size()));
    h.update(std::string_view("\0", 1));
  }
  return h.hex();
}

}  // namespace rpg
