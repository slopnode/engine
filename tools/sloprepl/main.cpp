#include <s7.h>
#include <iostream>
#include <string>
#include <optional> 
#include <filesystem>

struct Options {
    bool load;
    bool eval;
    std::filesystem::path file;
    Options() : load(false), eval(false) {}
};

std::optional<Options> parseArgs(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char*) -> const char* {
            if (i + 1 >= argc) {
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--load") {
            const char* value = needValue("--load");
            if (value == nullptr) {
                return std::nullopt;
            }
            options.eval = false;
            options.load = true;
            options.file = value;
            break;
        }
        if (arg == "--eval") {
            const char* value = needValue("--load");
            if (value == nullptr) {
                return std::nullopt;
            }
            options.eval = true;
            options.load = false;
            options.file = value;
            break;
        }
    }
    return options;
}

int main(int argc, char* argv[]) {
    const auto options = parseArgs(argc, argv);
    s7_scheme * sc = s7_init();

    if (options->eval || options->load) {
        s7_pointer eval_result = s7_load(sc, options->file.string().c_str());
        s7_display(sc, eval_result, s7_current_output_port(sc));
        std::cout << std::endl;
    }
    if (options && !options->eval) {
        std::string input;
        while (true) {
            std::cout << " > " << std::flush;
            if (!std::getline(std::cin, input)) {
                break;
            }
            if (input == "(quit)" || input == "(exit)") {
                break;
            }
            s7_pointer result = s7_eval_c_string(sc, input.c_str());
            if (result) {
                std::string c_result = s7_object_to_c_string(sc, result);
                std::cout << " < " << c_result << std::endl;
            }
        }
    }
    s7_quit(sc);
    std::cout << std::endl;
    return 0;
}