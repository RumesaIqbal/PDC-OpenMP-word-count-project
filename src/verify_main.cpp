#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct FileData {
    std::vector<std::string> lines;
    std::uint64_t digest = 14695981039346656037ull;  // FNV-1a offset basis
};

FileData read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open '" + path + "'");
    }
    FileData data;
    std::string line;
    while (std::getline(in, line)) {
        data.lines.push_back(line);
        for (const char c : line) {
            data.digest ^= static_cast<unsigned char>(c);
            data.digest *= 1099511628211ull;
        }
        data.digest ^= '\n';
        data.digest *= 1099511628211ull;
    }
    return data;
}

void usage() {
    std::cerr << "usage: wc_verify <a.tsv> <b.tsv> [--max-diff N]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args;
        int max_diff = 10;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--max-diff") {
                if (i + 1 >= argc) {
                    std::cerr << "error: missing value for --max-diff\n";
                    usage();
                    return 2;
                }
                max_diff = std::stoi(argv[++i]);
                if (max_diff < 0) {
                    max_diff = 0;
                }
            } else {
                args.push_back(arg);
            }
        }
        if (args.size() != 2) {
            usage();
            return 2;
        }

        const FileData a = read_file(args[0]);
        const FileData b = read_file(args[1]);
        std::cout << "A: " << args[0] << " (" << a.lines.size() << " rows, digest "
                  << std::hex << a.digest << std::dec << ")\n";
        std::cout << "B: " << args[1] << " (" << b.lines.size() << " rows, digest "
                  << std::hex << b.digest << std::dec << ")\n";

        bool match = a.lines.size() == b.lines.size();
        int shown = 0;
        for (std::size_t i = 0; i < a.lines.size() && i < b.lines.size(); ++i) {
            if (a.lines[i] != b.lines[i]) {
                match = false;
                if (shown < max_diff) {
                    std::cout << "diff at row " << (i + 1) << ":\n  A: " << a.lines[i]
                              << "\n  B: " << b.lines[i] << '\n';
                    ++shown;
                }
            }
        }
        std::cout << (match ? "MATCH" : "MISMATCH") << '\n';
        return match ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
