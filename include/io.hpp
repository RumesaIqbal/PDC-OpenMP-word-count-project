#pragma once

#include "tokenizer.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wc {

inline std::uint64_t file_size(const std::string& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(std::filesystem::path(path), ec);
    if (ec) {
        throw std::runtime_error("cannot stat '" + path + "': " + ec.message());
    }
    return static_cast<std::uint64_t>(n);
}

// Move an offset forward until it points at a delimiter byte. Because every
// segment starts at a delimiter and ends just before one, no word can be
// split between two chunks. Returns `size` if no delimiter is found.
inline std::uint64_t snap_forward(const std::string& path,
                                  std::uint64_t offset,
                                  std::uint64_t size) {
    if (offset == 0) {
        return 0;
    }
    if (offset >= size) {
        return size;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open '" + path + "' for boundary scan");
    }
    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) {
        throw std::runtime_error("cannot seek to offset " + std::to_string(offset));
    }
    std::uint64_t pos = offset;
    char c = '\0';
    while (in.get(c)) {
        if (is_delim(static_cast<unsigned char>(c))) {
            return pos;
        }
        ++pos;
    }
    return size;
}

// Returns num_chunks+1 offsets; consecutive offsets delimit one chunk.
// Duplicate boundaries (long tokens) are removed, so the returned vector may
// contain fewer chunks than requested.
inline std::vector<std::uint64_t> compute_boundaries(const std::string& path,
                                                     std::uint64_t size,
                                                     std::size_t num_chunks) {
    if (num_chunks == 0) {
        num_chunks = 1;
    }
    std::vector<std::uint64_t> bounds;
    bounds.reserve(num_chunks + 1);
    bounds.push_back(0);
    for (std::size_t i = 1; i < num_chunks; ++i) {
        const std::uint64_t raw =
            static_cast<std::uint64_t>((static_cast<unsigned long long>(size) * i) / num_chunks);
        const std::uint64_t snapped = snap_forward(path, raw, size);
        if (snapped > bounds.back() && snapped < size) {
            bounds.push_back(snapped);
        }
    }
    if (bounds.back() < size) {
        bounds.push_back(size);
    }
    return bounds;
}

// Bounded-memory reader for one byte range of the input file.
class ChunkReader {
public:
    ChunkReader(const std::string& path,
                std::uint64_t begin,
                std::uint64_t end,
                std::size_t buffer_bytes)
        : in_(path, std::ios::binary),
          pos_(begin),
          end_(end),
          buffer_(buffer_bytes) {
        if (buffer_bytes == 0) {
            throw std::runtime_error("buffer size must be positive");
        }
        if (!in_) {
            throw std::runtime_error("cannot open '" + path + "' for reading");
        }
        in_.seekg(static_cast<std::streamoff>(begin));
        if (!in_) {
            throw std::runtime_error("cannot seek to offset " + std::to_string(begin));
        }
    }

    bool next(const char*& data, std::size_t& len) {
        if (pos_ >= end_) {
            return false;
        }
        const std::size_t want = static_cast<std::size_t>(
            std::min<std::uint64_t>(static_cast<std::uint64_t>(buffer_.size()), end_ - pos_));
        in_.read(buffer_.data(), static_cast<std::streamsize>(want));
        const std::streamsize got = in_.gcount();
        if (got <= 0) {
            return false;
        }
        pos_ += static_cast<std::uint64_t>(got);
        data = buffer_.data();
        len = static_cast<std::size_t>(got);
        return true;
    }

private:
    std::ifstream in_;
    std::uint64_t pos_;
    std::uint64_t end_;
    std::vector<char> buffer_;
};

} // namespace wc
