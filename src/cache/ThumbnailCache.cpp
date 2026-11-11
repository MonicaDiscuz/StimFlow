#include "cache/ThumbnailCache.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>

namespace sf {
namespace {

constexpr std::uint32_t kMagic = 0x53464331;  // "SFC1"
constexpr std::uint32_t kVersion = 1;

#pragma pack(push, 1)
struct Header {
  std::uint32_t magic = kMagic;
  std::uint32_t version = kVersion;
  std::uint64_t mtime = 0;
  std::uint64_t file_size = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t data_size = 0;
  std::uint32_t reserved = 0;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 40);

DATA_BLOB Entropy() {
  static const char kEntropy[] = "StimFlow.ThumbnailCache.v1";
  DATA_BLOB blob{};
  blob.cbData = static_cast<DWORD>(sizeof(kEntropy) - 1);
  blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
  return blob;
}

bool Protect(const std::vector<std::uint8_t>& input, std::vector<std::uint8_t>& output) {
  DATA_BLOB in{};
  in.cbData = static_cast<DWORD>(input.size());
  in.pbData = const_cast<BYTE*>(input.data());
  DATA_BLOB entropy = Entropy();
  DATA_BLOB out{};
  if (!CryptProtectData(&in, L"StimFlow thumbnail", &entropy, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &out)) {
    return false;
  }
  output.assign(out.pbData, out.pbData + out.cbData);
  LocalFree(out.pbData);
  return true;
}

bool Unprotect(const std::vector<std::uint8_t>& input, std::vector<std::uint8_t>& output) {
  DATA_BLOB in{};
  in.cbData = static_cast<DWORD>(input.size());
  in.pbData = const_cast<BYTE*>(input.data());
  DATA_BLOB entropy = Entropy();
  DATA_BLOB out{};
  if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &out)) {
    return false;
  }
  output.assign(out.pbData, out.pbData + out.cbData);
  LocalFree(out.pbData);
  return true;
}

std::string Sha256Hex(const std::wstring& text) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
    return {};
  }

  DWORD object_size = 0;
  DWORD hash_size = 0;
  DWORD written = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &written,
                        0) != 0 ||
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_size),
                        sizeof(hash_size), &written, 0) != 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }

  std::vector<UCHAR> object(object_size);
  std::vector<UCHAR> digest(hash_size);
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string result;
  if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) == 0) {
    if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(text.data())),
                       static_cast<ULONG>(text.size() * sizeof(wchar_t)), 0) == 0 &&
        BCryptFinishHash(hash, digest.data(), hash_size, 0) == 0) {
      static constexpr char kHex[] = "0123456789abcdef";
      result.resize(static_cast<std::size_t>(hash_size) * 2);
      for (DWORD i = 0; i < hash_size; ++i) {
        result[i * 2] = kHex[digest[i] >> 4];
        result[i * 2 + 1] = kHex[digest[i] & 0x0f];
      }
    }
    BCryptDestroyHash(hash);
  }

  BCryptCloseAlgorithmProvider(algorithm, 0);
  return result;
}

std::wstring CacheKey(const std::filesystem::path& source) {
  std::error_code error;
  std::filesystem::path absolute = std::filesystem::absolute(source, error);
  if (error) {
    absolute = source;
  }
  std::wstring key = absolute.lexically_normal().wstring();
  if (!key.empty()) {
    CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
  }
  return key;
}

std::uint64_t SourceMTime(const std::filesystem::path& source) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(source, error);
  if (error) {
    return 0;
  }
  return static_cast<std::uint64_t>(time.time_since_epoch().count());
}

std::uint64_t SourceSize(const std::filesystem::path& source) {
  std::error_code error;
  const auto size = std::filesystem::file_size(source, error);
  if (error) {
    return 0;
  }
  return static_cast<std::uint64_t>(size);
}

bool ReadFile(const std::filesystem::path& path, std::vector<std::uint8_t>& data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return false;
  }
  const std::streamoff size = file.tellg();
  if (size <= 0) {
    return false;
  }
  data.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(data.data()), size);
  return static_cast<bool>(file);
}

bool WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  file.write(reinterpret_cast<const char*>(data.data()),
             static_cast<std::streamsize>(data.size()));
  file.flush();
  return static_cast<bool>(file);
}

}  // namespace

void ThumbnailCache::Open(const std::filesystem::path& folder) {
  Disable();
  if (folder.empty()) {
    return;
  }

  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error) {
    return;
  }

  const std::filesystem::path probe = folder / L".stimflow-write-test";
  {
    std::ofstream file(probe, std::ios::binary | std::ios::trunc);
    if (!file) {
      return;
    }
    file << 'x';
  }
  std::filesystem::remove(probe, error);

  folder_ = folder;
  enabled_ = true;
}

void ThumbnailCache::Disable() noexcept {
  folder_.clear();
  enabled_ = false;
  session_files_.clear();
}

std::filesystem::path ThumbnailCache::FileFor(const std::filesystem::path& source) const {
  if (folder_.empty()) {
    return {};
  }
  const std::string hash = Sha256Hex(CacheKey(source));
  if (hash.empty()) {
    return {};
  }
  std::wstring name(hash.begin(), hash.end());
  name += L".sfc";
  return folder_ / name;
}

bool ThumbnailCache::Load(const std::filesystem::path& source,
                          std::vector<std::uint8_t>& pixels, std::uint32_t& width,
                          std::uint32_t& height) const {
  if (!enabled_) {
    return false;
  }
  const std::filesystem::path file = FileFor(source);
  std::error_code error;
  if (file.empty() || !std::filesystem::exists(file, error)) {
    return false;
  }

  const std::uint64_t mtime = SourceMTime(source);
  const std::uint64_t file_size = SourceSize(source);
  if (mtime == 0 || file_size == 0) {
    return false;
  }

  std::vector<std::uint8_t> encrypted;
  if (!ReadFile(file, encrypted) || encrypted.empty()) {
    std::filesystem::remove(file, error);
    return false;
  }

  std::vector<std::uint8_t> plain;
  if (!Unprotect(encrypted, plain) || plain.size() < sizeof(Header)) {
    std::filesystem::remove(file, error);
    return false;
  }

  Header header{};
  std::memcpy(&header, plain.data(), sizeof(header));
  const std::uint64_t expected_size =
      static_cast<std::uint64_t>(header.width) * header.height * 4;
  if (header.magic != kMagic || header.version != kVersion || header.mtime != mtime ||
      header.file_size != file_size || header.width == 0 || header.height == 0 ||
      header.data_size != expected_size ||
      plain.size() != sizeof(Header) + header.data_size) {
    std::filesystem::remove(file, error);
    return false;
  }

  pixels.assign(plain.begin() + sizeof(Header), plain.end());
  width = header.width;
  height = header.height;
  return true;
}

void ThumbnailCache::Store(const std::filesystem::path& source,
                           const std::vector<std::uint8_t>& pixels, std::uint32_t width,
                           std::uint32_t height) {
  if (!enabled_ || width == 0 || height == 0) {
    return;
  }

  const std::uint64_t mtime = SourceMTime(source);
  const std::uint64_t file_size = SourceSize(source);
  const std::uint64_t data_size = static_cast<std::uint64_t>(width) * height * 4;
  if (mtime == 0 || file_size == 0 || pixels.size() < data_size) {
    return;
  }

  const std::filesystem::path file = FileFor(source);
  if (file.empty()) {
    return;
  }

  Header header{};
  header.mtime = mtime;
  header.file_size = file_size;
  header.width = width;
  header.height = height;
  header.data_size = static_cast<std::uint32_t>(data_size);

  std::vector<std::uint8_t> plain(sizeof(Header) + static_cast<std::size_t>(data_size));
  std::memcpy(plain.data(), &header, sizeof(header));
  std::memcpy(plain.data() + sizeof(header), pixels.data(), static_cast<std::size_t>(data_size));

  std::vector<std::uint8_t> encrypted;
  if (!Protect(plain, encrypted) || encrypted.empty()) {
    return;
  }

  const std::filesystem::path temporary = file.wstring() + L".tmp";
  if (!WriteFile(temporary, encrypted)) {
    std::error_code error;
    std::filesystem::remove(temporary, error);
    return;
  }
  if (!MoveFileExW(temporary.c_str(), file.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code error;
    std::filesystem::remove(temporary, error);
    return;
  }

  if (std::find(session_files_.begin(), session_files_.end(), file) == session_files_.end()) {
    session_files_.push_back(file);
  }
}

void ThumbnailCache::ClearSession() {
  std::error_code error;
  for (const std::filesystem::path& file : session_files_) {
    std::filesystem::remove(file, error);
  }
  session_files_.clear();
}

void ThumbnailCache::ClearAll() {
  std::error_code error;
  if (!folder_.empty() && std::filesystem::exists(folder_, error)) {
    for (std::filesystem::directory_iterator it(folder_, error), end; !error && it != end;
         it.increment(error)) {
      if (!it->is_regular_file(error)) {
        continue;
      }
      const std::filesystem::path& path = it->path();
      const std::wstring extension = path.extension().wstring();
      if (extension == L".sfc" || extension == L".tmp") {
        std::filesystem::remove(path, error);
      }
    }
  }
  session_files_.clear();
}

}  // namespace sf
