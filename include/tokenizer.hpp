#pragma once

#include "types.hpp"

#include <string>
#include <utility>

namespace wc {

// Normalization contract (frozen with the project plan):
//   word bytes  = [A-Za-z0-9']
//   delimiter   = anything else (all non-ASCII UTF-8 bytes are delimiters)
//   normalize   = ASCII lowercase
// Both wc_seq and wc_par MUST use this header so their outputs are identical.
inline bool is_delim(unsigned char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '\'');
}

inline unsigned char to_lower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

// Streams bytes through a fixed-size buffer. A token that crosses a buffer
// boundary is held in `carry_` and finished on the next feed() or by finish().
class Tokenizer {
public:
    explicit Tokenizer(Counter& counter) : counter_(counter) {
        carry_.reserve(256);
    }

    void feed(const char* data, std::size_t len) {
        std::size_t i = 0;
        while (i < len) {
            if (is_delim(static_cast<unsigned char>(data[i]))) {
                flush();
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            while (j < len && !is_delim(static_cast<unsigned char>(data[j]))) {
                ++j;
            }
            // A token that reaches the end of this buffer may continue in the
            // next one, so it stays in carry_ until a delimiter or finish().
            if (carry_.empty() && j < len) {
                std::string token;
                token.reserve(j - i);
                append_normalized(token, data + i, j - i);
                ++counter_[std::move(token)];
                ++tokens_;
            } else {
                append_normalized(carry_, data + i, j - i);
                if (j < len) {
                    flush();
                }
            }
            i = j;
        }
    }

    // Flush a token that ended at EOF or at a segment boundary.
    void finish() { flush(); }

    Count tokens() const noexcept { return tokens_; }

private:
    static void append_normalized(std::string& dst, const char* data, std::size_t len) {
        dst.reserve(dst.size() + len);
        for (std::size_t k = 0; k < len; ++k) {
            dst.push_back(static_cast<char>(to_lower(static_cast<unsigned char>(data[k]))));
        }
    }

    void flush() {
        if (carry_.empty()) {
            return;
        }
        ++counter_[std::move(carry_)];
        carry_.clear();
        ++tokens_;
    }

    Counter& counter_;
    std::string carry_;
    Count tokens_ = 0;
};

} // namespace wc
