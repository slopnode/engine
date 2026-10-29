#include "html_writer.hpp"

#include "embedded.hpp"
#include "highlight.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace slopdocs {

namespace fs = std::filesystem;

namespace {

struct TocEntry {
    int level = 1;
    std::string id;
    std::string title;
};

struct SearchEntry {
    std::string title;
    std::string url;
    std::string page;
};

struct PageContext {
    const Site* site = nullptr;
    const Page* page = nullptr;
    std::string rel;
    std::unordered_set<std::string> usedIds;
    std::vector<TocEntry> toc;
    std::vector<SearchEntry>* search = nullptr;
    std::string error;
    int depth = 0;
};

std::string slugify(const std::string& text) {
    std::string out;
    bool dash = false;
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
            dash = false;
        } else if (!out.empty() && !dash) {
            out.push_back('-');
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? "section" : out;
}

std::string uniqueId(PageContext& ctx, const std::string& base) {
    std::string id = base;
    int n = 2;
    while (ctx.usedIds.count(id) > 0) {
        id = base + "-" + std::to_string(n++);
    }
    ctx.usedIds.insert(id);
    return id;
}

std::string pageUrl(const std::string& id) {
    return id + ".html";
}

std::string relPrefix(const std::string& id) {
    std::string rel;
    for (char c : id) {
        if (c == '/') {
            rel += "../";
        }
    }
    return rel;
}

std::string engineAnchor(const std::string& name) {
    std::string anchor;
    for (char c : name) {
        if (c != '?' && c != '!') {
            anchor.push_back(c);
        }
    }
    return anchor;
}

std::string resolveSymbol(const Site& site, const std::string& name) {
    if (!site.config.engineDocs.empty() && site.engineNames.count(name) > 0) {
        return site.config.engineDocs + "scriptingapi.html#" + engineAnchor(name);
    }
    return {};
}

std::string classAttr(const DocNode& node, const std::string& base) {
    std::string cls = base;
    const std::string* extra = node.attr("class");
    if (extra != nullptr && !extra->empty()) {
        cls += " " + *extra;
    }
    std::string out = " class=\"" + escapeHtml(cls) + "\"";
    const std::string* id = node.attr("id");
    if (id != nullptr && node.tag != "section" && node.tag != "page") {
        out += " id=\"" + escapeHtml(*id) + "\"";
    }
    return out;
}

std::string plainText(const DocNode& node) {
    if (node.isText) {
        return collapseWhitespace(node.text);
    }
    std::string out;
    for (const DocNode& child : node.children) {
        out += plainText(child);
    }
    return out;
}

void renderInline(PageContext& ctx, const DocNode& node, std::string& out);
void renderBlock(PageContext& ctx, const DocNode& node, std::string& out);

void renderInlineChildren(PageContext& ctx, const DocNode& node, std::string& out) {
    for (const DocNode& child : node.children) {
        renderInline(ctx, child, out);
    }
}

void renderBlockChildren(PageContext& ctx, const DocNode& node, std::string& out) {
    for (const DocNode& child : node.children) {
        renderBlock(ctx, child, out);
    }
}

std::string codeText(const DocNode& node) {
    std::string text;
    for (const DocNode& child : node.children) {
        text += child.text;
    }
    return text;
}

void renderCodeBody(PageContext& ctx, const DocNode& node, const std::string& text, std::string& out) {
    const std::string* lang = node.attr("lang");
    if (lang != nullptr && *lang == "scheme") {
        const Site& site = *ctx.site;
        out += highlightScheme(text, [&site](std::string_view name) {
            return resolveSymbol(site, std::string(name));
        });
    } else {
        out += escapeHtml(text);
    }
}

void renderInline(PageContext& ctx, const DocNode& node, std::string& out) {
    if (node.isText) {
        out += escapeHtml(collapseWhitespace(node.text));
        return;
    }
    const std::string& tag = node.tag;
    if (tag == "em" || tag == "strong" || tag == "kbd") {
        out += "<" + tag + classAttr(node, "sd-" + tag) + ">";
        renderInlineChildren(ctx, node, out);
        out += "</" + tag + ">";
    } else if (tag == "code") {
        out += "<code" + classAttr(node, "sd-code") + ">";
        renderCodeBody(ctx, node, codeText(node), out);
        out += "</code>";
    } else if (tag == "link") {
        const std::string* href = node.attr("href");
        out += "<a" + classAttr(node, "sd-link") + " href=\"" + escapeHtml(href ? *href : "") + "\">";
        if (node.children.empty()) {
            out += escapeHtml(href ? *href : "");
        } else {
            renderInlineChildren(ctx, node, out);
        }
        out += "</a>";
    } else if (tag == "page-ref") {
        const std::string* id = node.attr("page");
        const Page* target = id ? ctx.site->findPage(*id) : nullptr;
        if (target == nullptr) {
            if (ctx.error.empty()) {
                ctx.error = "page-ref to unknown page \"" + (id ? *id : std::string()) + "\"";
            }
            return;
        }
        out += "<a" + classAttr(node, "sd-page-ref") + " href=\"" + escapeHtml(ctx.rel + pageUrl(target->id)) + "\">";
        if (node.children.empty()) {
            out += escapeHtml(target->title);
        } else {
            renderInlineChildren(ctx, node, out);
        }
        out += "</a>";
    } else if (tag == "ref") {
        const std::string* name = node.attr("name");
        const std::string href = name ? resolveSymbol(*ctx.site, *name) : std::string();
        if (href.empty()) {
            if (ctx.error.empty()) {
                ctx.error = "ref to unknown symbol \"" + (name ? *name : std::string()) + "\"";
            }
            return;
        }
        out += "<a" + classAttr(node, "sd-ref") + " href=\"" + escapeHtml(href) + "\"><code>" +
               escapeHtml(*name) + "</code></a>";
    } else if (tag == "br") {
        out += "<br>";
    } else if (tag == "html") {
        out += codeText(node);
    } else {
        if (ctx.error.empty()) {
            ctx.error = "node " + tag + " cannot be used inline";
        }
    }
}

void renderSection(PageContext& ctx, const DocNode& node, std::string& out) {
    const std::string* title = node.attr("title");
    const std::string* explicitId = node.attr("id");
    const std::string id = uniqueId(ctx, explicitId ? *explicitId : slugify(title ? *title : ""));
    ctx.depth += 1;
    const int level = std::min(ctx.depth + 1, 6);
    ctx.toc.push_back({ctx.depth, id, title ? *title : ""});
    if (ctx.search != nullptr && title != nullptr) {
        ctx.search->push_back({*title, pageUrl(ctx.page->id) + "#" + id, ctx.page->title});
    }
    out += "<section" + classAttr(node, "sd-section sd-section-" + std::to_string(ctx.depth)) +
           " id=\"" + escapeHtml(id) + "\">";
    out += "<h" + std::to_string(level) + " class=\"sd-heading\"><a class=\"sd-anchor\" href=\"#" +
           escapeHtml(id) + "\">" + escapeHtml(title ? *title : "") + "</a></h" + std::to_string(level) + ">";
    renderBlockChildren(ctx, node, out);
    out += "</section>\n";
    ctx.depth -= 1;
}

void renderBlock(PageContext& ctx, const DocNode& node, std::string& out) {
    const std::string& tag = node.tag;
    if (tag == "section") {
        renderSection(ctx, node, out);
    } else if (tag == "p") {
        out += "<p" + classAttr(node, "sd-p") + ">";
        renderInlineChildren(ctx, node, out);
        out += "</p>\n";
    } else if (tag == "code") {
        const std::string* lang = node.attr("lang");
        out += "<pre" + classAttr(node, "sd-code sd-code-block" + (lang ? " sd-lang-" + *lang : std::string())) + "><code>";
        renderCodeBody(ctx, node, dedentCode(codeText(node)), out);
        out += "</code></pre>\n";
    } else if (tag == "ul" || tag == "ol") {
        out += "<" + tag + classAttr(node, "sd-list sd-" + tag) + ">";
        for (const DocNode& item : node.children) {
            out += "<li" + classAttr(item, "sd-item") + ">";
            const bool single = item.children.size() == 1 && !item.children.front().isText &&
                                item.children.front().tag == "p";
            if (single) {
                renderInlineChildren(ctx, item.children.front(), out);
            } else {
                renderBlockChildren(ctx, item, out);
            }
            out += "</li>";
        }
        out += "</" + tag + ">\n";
    } else if (tag == "table") {
        out += "<table" + classAttr(node, "sd-table") + ">";
        for (const DocNode& rowNode : node.children) {
            const bool head = rowNode.tag == "header";
            out += head ? "<thead><tr>" : "<tr>";
            for (const DocNode& cell : rowNode.children) {
                const char* cellTag = head ? "th" : "td";
                out += std::string("<") + cellTag + classAttr(cell, "sd-cell") + ">";
                renderInlineChildren(ctx, cell, out);
                out += std::string("</") + cellTag + ">";
            }
            out += head ? "</tr></thead>" : "</tr>";
        }
        out += "</table>\n";
    } else if (tag == "dl") {
        out += "<dl" + classAttr(node, "sd-dl") + ">";
        for (const DocNode& entry : node.children) {
            const std::string* term = entry.attr("term");
            out += "<dt class=\"sd-term\">" + escapeHtml(term ? *term : "") + "</dt><dd class=\"sd-desc\">";
            renderInlineChildren(ctx, entry, out);
            out += "</dd>";
        }
        out += "</dl>\n";
    } else if (tag == "note" || tag == "tip" || tag == "warning") {
        out += "<aside" + classAttr(node, "sd-admonition sd-" + tag) + ">";
        out += "<p class=\"sd-admonition-title\">" + std::string(tag == "note" ? "Note" : tag == "tip" ? "Tip" : "Warning") + "</p>";
        renderBlockChildren(ctx, node, out);
        out += "</aside>\n";
    } else if (tag == "quote-block") {
        out += "<blockquote" + classAttr(node, "sd-quote") + ">";
        renderBlockChildren(ctx, node, out);
        out += "</blockquote>\n";
    } else if (tag == "image") {
        const std::string* src = node.attr("src");
        const std::string* alt = node.attr("alt");
        const std::string source = src ? *src : "";
        const bool external = source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
        out += "<figure" + classAttr(node, "sd-image") + "><img src=\"" +
               escapeHtml(external ? source : ctx.rel + source) + "\" alt=\"" + escapeHtml(alt ? *alt : "") + "\">";
        if (!node.children.empty()) {
            out += "<figcaption class=\"sd-caption\">";
            renderInlineChildren(ctx, node, out);
            out += "</figcaption>";
        }
        out += "</figure>\n";
    } else if (tag == "hr") {
        out += "<hr" + classAttr(node, "sd-hr") + ">\n";
    } else if (tag == "package-info") {
        const slopengine::PackageMeta& meta = ctx.site->meta;
        out += "<table" + classAttr(node, "sd-table sd-package-info") + ">";
        auto rowOf = [&](const char* field, const std::string& value) {
            out += "<tr><th class=\"sd-cell\">" + std::string(field) + "</th><td class=\"sd-cell\">" + value + "</td></tr>";
        };
        rowOf("Id", "<code class=\"sd-code\">" + escapeHtml(meta.id) + "</code>");
        rowOf("Name", escapeHtml(meta.name));
        rowOf("Version", escapeHtml(meta.version));
        std::string depends;
        for (const slopengine::PackageDependency& dep : meta.depends) {
            if (!depends.empty()) {
                depends += ", ";
            }
            depends += "<code class=\"sd-code\">" + escapeHtml(dep.id);
            if (!dep.versionConstraint.empty()) {
                depends += "@" + escapeHtml(dep.versionConstraint);
            }
            depends += "</code>";
        }
        rowOf("Depends", depends.empty() ? std::string("none") : depends);
        out += "</table>\n";
    } else if (tag == "html") {
        out += codeText(node);
        out += "\n";
    } else {
        if (ctx.error.empty()) {
            ctx.error = "node " + tag + " cannot be used as a block";
        }
    }
}

std::string renderNav(const Site& site, const std::string& currentId, const std::string& rel) {
    std::string out = "<nav id=\"sd-nav\"><ul class=\"sd-nav-list\">";
    auto link = [&](const std::string& id) {
        const Page* page = site.findPage(id);
        const std::string title = page != nullptr ? page->title : id == "index" ? std::string("Home") : id;
        const bool current = id == currentId;
        out += "<li class=\"sd-nav-item" + std::string(current ? " sd-current" : "") + "\"><a href=\"" +
               escapeHtml(rel + pageUrl(id)) + "\">" + escapeHtml(title) + "</a></li>";
    };
    for (const NavEntry& entry : site.config.nav) {
        if (entry.group) {
            out += "<li class=\"sd-nav-group\"><span class=\"sd-nav-group-label\">" + escapeHtml(entry.label) + "</span><ul>";
            for (const std::string& id : entry.pages) {
                link(id);
            }
            out += "</ul></li>";
        } else {
            for (const std::string& id : entry.pages) {
                link(id);
            }
        }
    }
    out += "</ul></nav>";
    return out;
}

std::string renderToc(const std::vector<TocEntry>& toc) {
    if (toc.size() < 2) {
        return {};
    }
    std::string out = "<nav id=\"sd-toc\"><p class=\"sd-toc-title\">On this page</p><ul>";
    int depth = 1;
    for (const TocEntry& entry : toc) {
        if (entry.level > 2) {
            continue;
        }
        while (depth < entry.level) {
            out += "<ul>";
            ++depth;
        }
        while (depth > entry.level) {
            out += "</ul>";
            --depth;
        }
        out += "<li><a href=\"#" + escapeHtml(entry.id) + "\">" + escapeHtml(entry.title) + "</a></li>";
    }
    while (depth > 1) {
        out += "</ul>";
        --depth;
    }
    out += "</ul></nav>";
    return out;
}

std::string renderShell(const Site& site, const std::string& pageId, const std::string& pageTitle,
                        const std::string& body, const std::string& toc) {
    const std::string rel = relPrefix(pageId);
    std::string out = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out += "<title>" + escapeHtml(pageTitle) + " - " + escapeHtml(site.config.title) + "</title>\n";
    out += "<link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">\n"
           "<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>\n"
           "<link rel=\"stylesheet\" href=\"https://fonts.googleapis.com/css2?family=Fira+Sans:ital,wght@0,400;0,600;0,700;1,400&family=JetBrains+Mono:wght@400;600&display=swap\">\n";
    out += "<link rel=\"stylesheet\" href=\"" + escapeHtml(rel) + "style.css\">\n";
    for (const std::string& sheet : site.config.stylesheets) {
        out += "<link rel=\"stylesheet\" href=\"" + escapeHtml(rel + sheet) + "\">\n";
    }
    out += "<script src=\"" + escapeHtml(rel) + "search-index.js\" defer></script>\n";
    out += "<script src=\"" + escapeHtml(rel) + "docs.js\" defer></script>\n";
    out += "</head>\n<body data-rel=\"" + escapeHtml(rel) + "\">\n";
    out += "<header id=\"sd-title\"><a class=\"sd-site-title\" href=\"" + escapeHtml(rel) + "index.html\">" +
           escapeHtml(site.config.title) + "</a>";
    if (!site.meta.version.empty()) {
        out += "<span class=\"sd-version\">" + escapeHtml(site.meta.version) + "</span>";
    }
    out += "<div id=\"sd-search\"><input type=\"search\" id=\"sd-search-input\" placeholder=\"Search\" autocomplete=\"off\">"
           "<ul id=\"sd-search-results\" hidden></ul></div></header>\n";
    out += "<div class=\"sd-layout\">\n";
    out += renderNav(site, pageId, rel);
    out += "\n<main id=\"sd-content\">\n<h1 class=\"sd-page-title\">" + escapeHtml(pageTitle) + "</h1>\n";
    out += toc;
    out += body;
    out += "</main>\n</div>\n</body>\n</html>\n";
    return out;
}

bool writeText(const fs::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write " + path.generic_string();
        return false;
    }
    out << text;
    return true;
}

std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                break;
            }
            out.push_back(c);
        }
    }
    out += "\"";
    return out;
}

bool copyTree(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "cannot copy " + from.generic_string() + ": " + ec.message();
        return false;
    }
    return true;
}

std::string generatedIndex(const Site& site) {
    std::string body = "<p class=\"sd-p\">" + escapeHtml(site.meta.name.empty() ? site.meta.id : site.meta.name);
    if (!site.meta.version.empty()) {
        body += " " + escapeHtml(site.meta.version);
    }
    body += "</p>\n<ul class=\"sd-list sd-ul\">";
    for (const Page& page : site.pages) {
        body += "<li class=\"sd-item\"><a href=\"" + escapeHtml(pageUrl(page.id)) + "\">" + escapeHtml(page.title) + "</a></li>";
    }
    body += "</ul>\n";
    return body;
}

}

bool writeSite(const Site& site, const fs::path& outDir, std::string& error, bool verbose) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        error = "cannot create " + outDir.generic_string();
        return false;
    }
    if (!writeText(outDir / "style.css", std::string(themeCss()), error)) {
        return false;
    }
    if (!writeText(outDir / "docs.js", std::string(docsJs()), error)) {
        return false;
    }
    for (const std::string& sheet : site.config.stylesheets) {
        fs::create_directories((outDir / sheet).parent_path(), ec);
        fs::copy_file(site.docsRoot / sheet, outDir / sheet, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "cannot copy stylesheet " + sheet + ": " + ec.message();
            return false;
        }
    }
    if (fs::is_directory(site.docsRoot / "images") && !copyTree(site.docsRoot / "images", outDir / "images", error)) {
        return false;
    }

    std::vector<SearchEntry> search;
    for (const Page& page : site.pages) {
        PageContext ctx;
        ctx.site = &site;
        ctx.page = &page;
        ctx.rel = relPrefix(page.id);
        ctx.search = &search;
        search.push_back({page.title, pageUrl(page.id), ""});
        std::string body;
        for (const DocNode& child : page.root.children) {
            renderBlock(ctx, child, body);
        }
        if (!ctx.error.empty()) {
            error = page.source.generic_string() + ": page \"" + page.id + "\": " + ctx.error;
            return false;
        }
        const std::string html = renderShell(site, page.id, page.title, body, renderToc(ctx.toc));
        if (!writeText(outDir / pageUrl(page.id), html, error)) {
            return false;
        }
        if (verbose) {
            std::cout << "slopdocs: wrote " << pageUrl(page.id) << "\n";
        }
    }
    if (site.findPage("index") == nullptr) {
        const std::string html = renderShell(site, "index", site.config.title, generatedIndex(site), "");
        if (!writeText(outDir / "index.html", html, error)) {
            return false;
        }
    }

    std::string index = "window.SD_INDEX = [";
    for (std::size_t i = 0; i < search.size(); ++i) {
        const SearchEntry& entry = search[i];
        index += "{\"t\":" + jsonString(entry.title) + ",\"u\":" + jsonString(entry.url) + ",\"p\":" + jsonString(entry.page) + "}";
        if (i + 1 < search.size()) {
            index += ",";
        }
    }
    index += "];\n";
    return writeText(outDir / "search-index.js", index, error);
}

}
