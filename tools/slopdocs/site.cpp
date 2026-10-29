#include "site.hpp"

#include "data_pages.hpp"
#include "embedded.hpp"
#include "script_pages.hpp"
#include "shader_pages.hpp"

#include "core/sexpr.hpp"
#include "script/scheme_harden.hpp"

#include <s7.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace slopdocs {

namespace fs = std::filesystem;

const Page* Site::findPage(const std::string& id) const {
    for (const Page& page : pages) {
        if (page.id == id) {
            return &page;
        }
    }
    return nullptr;
}

const ShaderDoc* Site::findShader(const std::string& path) const {
    for (const ShaderDoc& shader : shaders) {
        if (shader.path == path) {
            return &shader;
        }
    }
    return nullptr;
}

namespace {

struct Loader {
    Site* site = nullptr;
    fs::path currentFile;
    std::unordered_set<std::string> loaded;
    std::string error;
    bool verbose = false;
};

Loader* g_loader = nullptr;

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool loadDocFile(s7_scheme* sc, Loader& loader, const fs::path& file) {
    const std::string key = fs::weakly_canonical(file).generic_string();
    if (loader.loaded.count(key) > 0) {
        return true;
    }
    loader.loaded.insert(key);
    const fs::path previous = loader.currentFile;
    loader.currentFile = file;
    if (loader.verbose) {
        std::cout << "slopdocs: loading " << file.generic_string() << "\n";
    }
    slopengine::clearScriptingErrors();
    s7_load(sc, file.string().c_str());
    loader.currentFile = previous;
    if (slopengine::scriptingErrorsOccurred()) {
        if (loader.error.empty()) {
            loader.error = file.generic_string() + ": evaluation failed";
        }
        return false;
    }
    return !loader.error.empty() ? false : true;
}

s7_pointer g_register_page(s7_scheme* sc, s7_pointer args) {
    Loader& loader = *g_loader;
    DocNode node;
    std::string error;
    if (!docNodeFromS7(sc, s7_car(args), node, error)) {
        return s7_error(sc, s7_make_symbol(sc, "slopdocs"), s7_list(sc, 1, s7_make_string(sc, error.c_str())));
    }
    const std::string* id = node.attr("id");
    const std::string* title = node.attr("title");
    if (id == nullptr || title == nullptr) {
        return s7_error(sc, s7_make_symbol(sc, "slopdocs"), s7_list(sc, 1, s7_make_string(sc, "page needs an id and a title")));
    }
    if (loader.site->findPage(*id) != nullptr) {
        const std::string message = "page \"" + *id + "\" is defined twice";
        return s7_error(sc, s7_make_symbol(sc, "slopdocs"), s7_list(sc, 1, s7_make_string(sc, message.c_str())));
    }
    if (!validateDocPage(node, error)) {
        const std::string message = "page \"" + *id + "\": " + error;
        return s7_error(sc, s7_make_symbol(sc, "slopdocs"), s7_list(sc, 1, s7_make_string(sc, message.c_str())));
    }
    Page page;
    page.id = *id;
    page.title = *title;
    page.root = std::move(node);
    page.source = loader.currentFile;
    loader.site->pages.push_back(std::move(page));
    return s7_t(sc);
}

s7_pointer g_require_doc(s7_scheme* sc, s7_pointer args) {
    Loader& loader = *g_loader;
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "require-doc", 1, s7_car(args), "string");
    }
    const fs::path file = loader.site->docsRoot / (std::string(s7_string(s7_car(args))) + ".s7");
    if (!fs::is_regular_file(file)) {
        const std::string message = "require-doc: no such file " + file.generic_string();
        return s7_error(sc, s7_make_symbol(sc, "slopdocs"), s7_list(sc, 1, s7_make_string(sc, message.c_str())));
    }
    if (!loadDocFile(sc, loader, file)) {
        return s7_f(sc);
    }
    return s7_t(sc);
}

bool parseNavEntry(const slopengine::Sexpr& form, NavEntry& out, std::string& error) {
    if (form.isString()) {
        out.pages = {form.text};
        return true;
    }
    if (!form.isList() || form.list.empty() || !form.list.front().isString()) {
        error = "nav entries are page ids or (\"Group\" id...)";
        return false;
    }
    out.group = true;
    out.label = form.list.front().text;
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        if (!form.list[i].isString()) {
            error = "nav group \"" + out.label + "\": entries are page ids";
            return false;
        }
        out.pages.push_back(form.list[i].text);
    }
    return true;
}

bool parseDocsMeta(const fs::path& path, SiteConfig& out, std::string& error) {
    std::string source;
    if (!readFile(path, source)) {
        error = "cannot read " + path.generic_string();
        return false;
    }
    const slopengine::SexprParseResult parsed = slopengine::parseSexprs(source);
    if (!parsed.ok) {
        error = slopengine::formatSexprError(path.generic_string(), parsed.error);
        return false;
    }
    for (const slopengine::Sexpr& form : parsed.forms) {
        if (!form.isList() || form.list.empty() || !form.list.front().isAtom("docs")) {
            continue;
        }
        for (std::size_t i = 1; i < form.list.size(); ++i) {
            const slopengine::Sexpr& field = form.list[i];
            if (!field.isList() || field.list.empty() || field.list.front().kind != slopengine::SexprKind::Atom) {
                error = path.generic_string() + ": docs fields are (name value...)";
                return false;
            }
            const std::string& name = field.list.front().text;
            if (name == "title") {
                if (field.list.size() != 2 || !field.list[1].isString()) {
                    error = path.generic_string() + ": title takes one string";
                    return false;
                }
                out.title = field.list[1].text;
            } else if (name == "nav") {
                for (std::size_t j = 1; j < field.list.size(); ++j) {
                    NavEntry entry;
                    if (!parseNavEntry(field.list[j], entry, error)) {
                        error = path.generic_string() + ": " + error;
                        return false;
                    }
                    out.nav.push_back(std::move(entry));
                }
            } else if (name == "stylesheet") {
                for (std::size_t j = 1; j < field.list.size(); ++j) {
                    if (!field.list[j].isString()) {
                        error = path.generic_string() + ": stylesheet takes file names";
                        return false;
                    }
                    out.stylesheets.push_back(field.list[j].text);
                }
            } else if (name == "engine-docs") {
                if (field.list.size() != 2 || !field.list[1].isString()) {
                    error = path.generic_string() + ": engine-docs takes one url";
                    return false;
                }
                out.engineDocs = field.list[1].text;
                if (!out.engineDocs.empty() && out.engineDocs.back() != '/') {
                    out.engineDocs.push_back('/');
                }
            } else {
                error = path.generic_string() + ": unknown docs field " + name;
                return false;
            }
        }
    }
    return true;
}

void loadEngineNames(Site& site) {
    std::istringstream in{std::string(engineNamesText())};
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            site.engineNames.insert(line);
        }
    }
}

NavEntry buildTree(const std::string& label, const std::string& prefix, const std::vector<std::string>& paths) {
    NavEntry node;
    node.group = true;
    node.label = label;
    std::map<std::string, std::vector<std::string>> folders;
    for (const std::string& path : paths) {
        const std::size_t slash = path.find('/');
        if (slash == std::string::npos) {
            node.pages.push_back(prefix + path);
        } else {
            folders[path.substr(0, slash)].push_back(path.substr(slash + 1));
        }
    }
    for (const auto& [folder, rest] : folders) {
        node.children.push_back(buildTree(folder, prefix + folder + "/", rest));
    }
    return node;
}

bool resolveNav(Site& site, std::string& error) {
    std::unordered_set<std::string> listed;
    for (const NavEntry& entry : site.config.nav) {
        for (const std::string& id : entry.pages) {
            if (site.findPage(id) == nullptr) {
                error = "docs.meta: nav names page \"" + id + "\" which no file defines";
                return false;
            }
            listed.insert(id);
        }
    }
    std::vector<std::string> rest;
    for (const Page& page : site.pages) {
        if (listed.count(page.id) == 0 && page.id != "index" && !page.generated) {
            rest.push_back(page.id);
        }
    }
    std::sort(rest.begin(), rest.end());
    if (listed.count("index") == 0) {
        NavEntry index;
        index.pages = {"index"};
        site.config.nav.insert(site.config.nav.begin(), std::move(index));
    }
    for (const std::string& id : rest) {
        NavEntry entry;
        entry.pages = {id};
        site.config.nav.push_back(std::move(entry));
    }
    if (!site.scripts.empty()) {
        std::vector<std::string> paths;
        for (const ScriptDoc& script : site.scripts) {
            paths.push_back(script.path);
        }
        site.config.nav.push_back(buildTree("Scripts", "scripts/", paths));
    }
    if (!site.data.empty()) {
        std::vector<std::string> paths;
        for (const DataDoc& data : site.data) {
            paths.push_back(data.path);
        }
        site.config.nav.push_back(buildTree("Data", "data/", paths));
    }
    if (!site.shaders.empty()) {
        std::vector<std::string> paths;
        for (const ShaderDoc& shader : site.shaders) {
            paths.push_back(shader.path);
        }
        NavEntry assets;
        assets.group = true;
        assets.label = "Assets";
        assets.children.push_back(buildTree("Shaders", "shaders/", paths));
        site.config.nav.push_back(std::move(assets));
    }
    return true;
}

}

bool loadSite(const fs::path& packageRoot, Site& out, std::string& error, bool verbose) {
    out.packageRoot = packageRoot;
    out.docsRoot = packageRoot / "docs";
    const auto meta = slopengine::loadPackageMetaFile(packageRoot / "package.meta");
    if (!meta) {
        error = "no readable package.meta in " + packageRoot.generic_string();
        return false;
    }
    out.meta = *meta;
    out.config.title = out.meta.name.empty() ? out.meta.id : out.meta.name;
    loadEngineNames(out);

    const fs::path metaPath = out.docsRoot / "docs.meta";
    if (fs::is_regular_file(metaPath) && !parseDocsMeta(metaPath, out.config, error)) {
        return false;
    }
    for (const std::string& sheet : out.config.stylesheets) {
        if (!fs::is_regular_file(out.docsRoot / sheet)) {
            error = "docs.meta: stylesheet " + sheet + " not found under docs/";
            return false;
        }
    }

    std::vector<fs::path> files;
    if (fs::is_directory(out.docsRoot)) {
        for (const auto& entry : fs::recursive_directory_iterator(out.docsRoot)) {
            if (entry.is_regular_file() && entry.path().extension() == ".s7") {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());

    s7_scheme* sc = s7_init();
    slopengine::hardenSchemeRuntime(sc);
    Loader loader;
    loader.site = &out;
    loader.verbose = verbose;
    g_loader = &loader;
    s7_define_function(sc, "sd-register-page", g_register_page, 1, 0, false, "(sd-register-page node)");
    s7_define_function(sc, "require-doc", g_require_doc, 1, 0, false, "(require-doc path)");
    slopengine::clearScriptingErrors();
    const std::string prelude = "(begin " + std::string(preludeSource()) + ")";
    s7_eval_c_string(sc, prelude.c_str());
    if (slopengine::scriptingErrorsOccurred()) {
        error = "slopdocs prelude failed to load";
        g_loader = nullptr;
        s7_quit(sc);
        return false;
    }

    bool ok = true;
    for (const fs::path& file : files) {
        if (!loadDocFile(sc, loader, file)) {
            ok = false;
            break;
        }
    }
    g_loader = nullptr;
    s7_quit(sc);
    if (!ok) {
        error = loader.error;
        return false;
    }
    out.scripts = scanScripts(packageRoot);
    out.data = scanData(packageRoot, verbose);
    out.shaders = scanShaders(packageRoot);
    if (!buildDataPages(out, error)) {
        return false;
    }
    if (!buildScriptPages(out, error)) {
        return false;
    }
    if (!buildShaderPages(out, error)) {
        return false;
    }
    out.counts = countAssets(packageRoot, verbose);
    return resolveNav(out, error);
}

}
