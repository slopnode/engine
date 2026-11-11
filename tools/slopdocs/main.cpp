#include "html_writer.hpp"
#include "site.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void printUsage() {
    std::cout << "Usage: slopdocs <package-path> --out <dir> [--verbose]\n\n"
                 "  <package-path>  Directory containing package.meta\n"
                 "  --out           Directory to write the HTML site into (required)\n"
                 "  --verbose       Print each file loaded and written\n";
}

}

int main(int argc, char* argv[]) {
    std::filesystem::path packagePath;
    std::filesystem::path outDir;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            outDir = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "slopdocs: unknown flag " << arg << "\n";
            printUsage();
            return 2;
        } else if (packagePath.empty()) {
            packagePath = arg;
        } else {
            printUsage();
            return 2;
        }
    }
    if (packagePath.empty() || outDir.empty()) {
        printUsage();
        return 2;
    }

    slopdocs::Site site;
    std::string error;
    if (!slopdocs::loadSite(packagePath, site, error, verbose)) {
        std::cerr << "slopdocs: " << error << "\n";
        return 1;
    }
    if (!slopdocs::writeSite(site, outDir, error, verbose)) {
        std::cerr << "slopdocs: " << error << "\n";
        return 1;
    }
    std::cout << "slopdocs: " << site.pages.size() << " page" << (site.pages.size() == 1 ? "" : "s")
              << " written to " << outDir.generic_string() << "\n";
    return 0;
}
