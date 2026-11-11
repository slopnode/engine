#include "data_pages.hpp"

#include "doc_text.hpp"
#include "script_pages.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace slopdocs {

namespace {

std::string cellText(const DataValue& v) {
    if (v.kind == DataValue::Kind::String) {
        return v.text;
    }
    if (v.kind == DataValue::Kind::List && !v.dotted) {
        std::string out;
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            if (i > 0) {
                out.push_back(' ');
            }
            out += writeDataValue(v.items[i]);
        }
        return out;
    }
    return writeDataValue(v);
}

std::string slug(const std::string& text) {
    std::string out;
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

bool isScalar(const DataValue& v) {
    return v.kind == DataValue::Kind::String || v.kind == DataValue::Kind::Number ||
           v.kind == DataValue::Kind::Bool || v.kind == DataValue::Kind::Symbol;
}

bool isPairLike(const DataValue& v) {
    return v.kind == DataValue::Kind::List && !v.items.empty() &&
           (v.items.front().kind == DataValue::Kind::Symbol || v.items.front().kind == DataValue::Kind::String) &&
           v.items.size() >= 2;
}

struct Field {
    std::string key;
    DataValue value;
};

DocNode keyNode(const std::string& key) {
    return withAttr(codeInline(key), "class", "sd-key");
}

std::vector<Field> alistFields(const DataValue& alist);

std::string entryTitle(const std::vector<Field>& fields, std::string& id) {
    std::string title;
    for (const Field& f : fields) {
        if (f.key == "id" && isScalar(f.value)) {
            id = cellText(f.value);
        }
    }
    for (const char* key : {"label", "title", "name"}) {
        for (const Field& f : fields) {
            if (f.key == key && isScalar(f.value)) {
                return cellText(f.value);
            }
        }
    }
    return id;
}

std::vector<std::string> headedKeys(const std::vector<Field>& fields, const std::string& title, const std::string& id) {
    std::vector<std::string> skip;
    for (const char* key : {"label", "title", "name"}) {
        for (const Field& f : fields) {
            if (f.key == key && isScalar(f.value) && cellText(f.value) == title) {
                skip.push_back(key);
            }
        }
    }
    if (!id.empty()) {
        skip.push_back("id");
    }
    return skip;
}

bool isScalarList(const DataValue& v) {
    return v.kind == DataValue::Kind::List && !v.dotted && !v.items.empty() &&
           std::all_of(v.items.begin(), v.items.end(), isScalar);
}

bool isTreeValue(const DataValue& v) {
    if (v.kind != DataValue::Kind::List || v.dotted || v.items.empty()) {
        return false;
    }
    bool anyPair = false;
    for (const DataValue& item : v.items) {
        if (isPairLike(item)) {
            anyPair = true;
        } else if (!isScalar(item)) {
            return false;
        }
    }
    return anyPair;
}

DocNode leafItem(const std::string& key, const DataValue& v) {
    std::vector<DocNode> line = {keyNode(key)};
    std::vector<DocNode> value;
    if (isScalar(v)) {
        value.push_back(textNode(cellText(v)));
    } else if (isScalarList(v)) {
        std::string joined;
        for (const DataValue& item : v.items) {
            if (!joined.empty()) {
                joined.push_back(' ');
            }
            joined += cellText(item);
        }
        value.push_back(textNode(joined));
    } else {
        value.push_back(codeInline(cellText(v)));
    }
    line.push_back(withAttr(elem("span", std::move(value)), "class", "sd-val"));
    return withAttr(elem("item", {elem("p", std::move(line))}), "class", "sd-leaf");
}

DocNode branchItem(const std::string& key, const std::string& extra, DocNode children) {
    std::vector<DocNode> line = {keyNode(key)};
    if (!extra.empty()) {
        line.push_back(withAttr(elem("span", {textNode(extra)}), "class", "sd-val"));
    }
    return withAttr(elem("item", {elem("p", std::move(line)), std::move(children)}), "class", "sd-branch");
}

DocNode treeFromFields(const std::vector<Field>& fields, const std::vector<std::string>& skip);

DocNode treeFromValue(const DataValue& v);

DocNode fieldItem(const std::string& key, const DataValue& v) {
    if (isCatalog(v)) {
        DocNode tree = withAttr(elem("ul"), "class", "sd-tree");
        for (const DataValue& entry : v.items) {
            DataValue alist;
            if (entry.dotted) {
                alist = entry.items[1];
            } else {
                alist.kind = DataValue::Kind::List;
                alist.items.assign(entry.items.begin() + 1, entry.items.end());
            }
            std::vector<Field> fields = alistFields(alist);
            std::string id = entry.items.front().text;
            std::string title = entryTitle(fields, id);
            id = entry.items.front().text;
            tree.children.push_back(branchItem(title, title == id ? std::string() : id, treeFromFields(fields, headedKeys(fields, title, id))));
        }
        return branchItem(key, std::string(), std::move(tree));
    }
    if (isAlistList(v)) {
        DocNode tree = withAttr(elem("ul"), "class", "sd-tree");
        std::size_t n = 0;
        for (const DataValue& entry : v.items) {
            ++n;
            std::vector<Field> fields = alistFields(entry);
            std::string id;
            std::string title = entryTitle(fields, id);
            if (title.empty()) {
                title = "#" + std::to_string(n);
            }
            tree.children.push_back(branchItem(title, title == id ? std::string() : id, treeFromFields(fields, headedKeys(fields, title, id))));
        }
        return branchItem(key, std::string(), std::move(tree));
    }
    if (isTreeValue(v)) {
        std::string scalars;
        for (const DataValue& item : v.items) {
            if (isScalar(item)) {
                if (!scalars.empty()) {
                    scalars.push_back(' ');
                }
                scalars += cellText(item);
            }
        }
        return branchItem(key, scalars, treeFromValue(v));
    }
    return leafItem(key, v);
}

DocNode treeFromFields(const std::vector<Field>& fields, const std::vector<std::string>& skip) {
    DocNode tree = withAttr(elem("ul"), "class", "sd-tree");
    for (const Field& f : fields) {
        if (std::find(skip.begin(), skip.end(), f.key) != skip.end()) {
            continue;
        }
        tree.children.push_back(fieldItem(f.key, f.value));
    }
    return tree;
}

DocNode treeFromValue(const DataValue& v) {
    DocNode tree = withAttr(elem("ul"), "class", "sd-tree");
    for (const DataValue& item : v.items) {
        if (!isPairLike(item)) {
            continue;
        }
        DataValue rest;
        if (item.dotted && item.items.size() == 2) {
            rest = item.items[1];
        } else {
            rest.kind = DataValue::Kind::List;
            rest.items.assign(item.items.begin() + 1, item.items.end());
            if (rest.items.size() == 1 && isScalar(rest.items.front())) {
                rest = rest.items.front();
            }
        }
        tree.children.push_back(fieldItem(item.items.front().text, rest));
    }
    return tree;
}

std::vector<Field> alistFields(const DataValue& alist) {
    std::vector<Field> out;
    for (const DataValue& entry : alist.items) {
        Field f;
        f.key = entry.items.front().text;
        if (entry.dotted && entry.items.size() == 2) {
            f.value = entry.items[1];
        } else {
            f.value.kind = DataValue::Kind::List;
            f.value.items.assign(entry.items.begin() + 1, entry.items.end());
            if (f.value.items.size() == 1 && isScalar(f.value.items.front())) {
                f.value = f.value.items.front();
            }
        }
        out.push_back(std::move(f));
    }
    return out;
}

DocNode entrySection(const std::string& symbol, const std::string& title, const std::string& id, const std::vector<Field>& fields, const std::string& groupKey) {
    const std::vector<std::string> skip = headedKeys(fields, title, id);
    DocNode entry = withAttr(elem("section"), "title", title);
    if (!id.empty() && id != title) {
        entry = withAttr(entry, "subtitle", id);
    }
    entry = withAttr(entry, "id", slug(symbol) + "-" + slug(id.empty() ? title : id));
    entry = withAttr(entry, "class", "sd-entry");
    DocNode props = withAttr(elem("ul"), "class", "sd-tree sd-entry-props");
    for (const Field& f : fields) {
        if (std::find(skip.begin(), skip.end(), f.key) != skip.end() || (!groupKey.empty() && f.key == groupKey)) {
            continue;
        }
        props.children.push_back(fieldItem(f.key, f.value));
    }
    if (!props.children.empty()) {
        entry.children.push_back(std::move(props));
    }
    return entry;
}

void renderCatalog(const std::string& symbol, const DataValue& value, std::vector<DocNode>& blocks) {
    std::vector<std::pair<std::string, std::vector<Field>>> rows;
    for (const DataValue& entry : value.items) {
        DataValue alist;
        if (entry.dotted) {
            alist = entry.items[1];
        } else {
            alist.kind = DataValue::Kind::List;
            alist.items.assign(entry.items.begin() + 1, entry.items.end());
        }
        rows.emplace_back(entry.items.front().text, alistFields(alist));
    }
    std::string groupKey;
    for (const char* candidate : {"path", "folder", "group", "category", "kind"}) {
        std::size_t have = 0;
        for (const auto& [id, fields] : rows) {
            for (const Field& f : fields) {
                if (f.key == candidate) {
                    ++have;
                    break;
                }
            }
        }
        if (have * 2 >= rows.size()) {
            groupKey = candidate;
            break;
        }
    }
    std::vector<std::string> groups;
    std::map<std::string, std::vector<const std::pair<std::string, std::vector<Field>>*>> byGroup;
    for (const auto& r : rows) {
        std::string group;
        for (const Field& f : r.second) {
            if (f.key == groupKey) {
                group = cellText(f.value);
                break;
            }
        }
        if (byGroup.count(group) == 0) {
            groups.push_back(group);
        }
        byGroup[group].push_back(&r);
    }
    for (const std::string& group : groups) {
        std::vector<DocNode> entries;
        for (const auto* r : byGroup[group]) {
            std::string id = r->first;
            const std::string title = entryTitle(r->second, id);
            entries.push_back(entrySection(symbol, title, r->first, r->second, groupKey));
        }
        if (groupKey.empty()) {
            for (DocNode& n : entries) {
                blocks.push_back(std::move(n));
            }
        } else {
            blocks.push_back(withAttr(withAttr(elem("section", std::move(entries)), "title", group.empty() ? "ungrouped" : group), "class", "sd-data-group"));
        }
    }
}

void renderAlistList(const std::string& symbol, const DataValue& value, std::vector<DocNode>& blocks) {
    std::size_t n = 0;
    for (const DataValue& entry : value.items) {
        ++n;
        std::vector<Field> fields = alistFields(entry);
        std::string id;
        std::string title = entryTitle(fields, id);
        if (title.empty()) {
            title = "#" + std::to_string(n);
        }
        blocks.push_back(entrySection(symbol, title, id, fields, std::string()));
    }
}

void renderValue(const std::string& symbol, const DataValue& value, std::vector<DocNode>& blocks, std::vector<std::pair<std::string, std::string>>& scalars) {
    if (isCatalog(value)) {
        renderCatalog(symbol, value, blocks);
        return;
    }
    if (isAlistList(value)) {
        renderAlistList(symbol, value, blocks);
        return;
    }
    if (isAlist(value)) {
        std::vector<Field> fields = alistFields(value);
        bool nested = false;
        for (const Field& f : fields) {
            if (isCatalog(f.value) || isAlistList(f.value) || isAlist(f.value)) {
                nested = true;
            }
        }
        if (nested) {
            for (const Field& f : fields) {
                if (isCatalog(f.value) || isAlistList(f.value) || isAlist(f.value)) {
                    std::vector<DocNode> inner;
                    std::vector<std::pair<std::string, std::string>> innerScalars;
                    renderValue(symbol + " " + f.key, f.value, inner, innerScalars);
                    for (auto& sc : innerScalars) {
                        inner.push_back(elem("p", {codeInline(sc.first), textNode(" "), codeInline(sc.second)}));
                    }
                    blocks.push_back(withAttr(withAttr(elem("section", std::move(inner)), "title", f.key), "class", "sd-data-group"));
                } else {
                    scalars.emplace_back(f.key, cellText(f.value));
                }
            }
            return;
        }
        blocks.push_back(withAttr(treeFromFields(fields, {}), "class", "sd-tree sd-entry-props"));
        return;
    }
    scalars.emplace_back(symbol, writeDataValue(value));
}

}

bool buildDataPages(Site& site, std::string& error) {
    for (const DataDoc& data : site.data) {
        if (!data.error.empty()) {
            error = data.error;
            return false;
        }
        const std::string id = "data/" + data.path;
        for (const DataDef& def : data.defs) {
            site.symbols.emplace(def.name, id + ".html#" + scriptAnchor(def.name));
        }
    }
    for (const DataDoc& data : site.data) {
        const std::string id = "data/" + data.path;
        if (site.findPage(id) != nullptr) {
            error = "page \"" + id + "\" collides with the generated data page";
            return false;
        }
        DocNode root = withAttr(withAttr(elem("page"), "id", id), "title", data.path);
        for (DocNode& block : docTextToBlocks(site, data.summary)) {
            root.children.push_back(std::move(block));
        }
        DocNode facts = withAttr(elem("table"), "class", "sd-script-facts");
        facts.children.push_back(row({cell({textNode("File")}), cell({codeInline("data/" + data.path + ".s7")})}));
        root.children.push_back(std::move(facts));
        if (!data.evaluated) {
            root.children.push_back(elem("p", {textNode("This file did not evaluate, so its values are not shown.")}));
        }

        struct Scalar {
            std::string name;
            std::string value;
            std::string doc;
        };
        std::vector<Scalar> scalars;
        std::vector<DocNode> sections;
        for (const DataDef& def : data.defs) {
            std::vector<DocNode> blocks = docTextToBlocks(site, def.doc);
            const std::size_t docBlocks = blocks.size();
            std::vector<std::pair<std::string, std::string>> own;
            if (def.evaluated) {
                renderValue(def.name, def.value, blocks, own);
            }
            if (blocks.size() > docBlocks) {
                for (auto& s : own) {
                    blocks.push_back(elem("p", {codeInline(s.first), textNode(" "), codeInline(s.second)}));
                }
                sections.push_back(withAttr(withAttr(elem("section", std::move(blocks)), "title", def.name), "id", scriptAnchor(def.name)));
            } else if (own.size() == 1 && own.front().first == def.name) {
                scalars.push_back({def.name, own.front().second, def.doc});
            } else {
                for (auto& s : own) {
                    scalars.push_back({s.first, s.second, std::string()});
                }
                if (own.empty()) {
                    scalars.push_back({def.name, def.evaluated ? std::string() : std::string("not evaluated"), def.doc});
                }
            }
        }
        if (!scalars.empty()) {
            DocNode table = withAttr(elem("table"), "class", "sd-data sd-data-values");
            table.children.push_back(headerRow({"Name", "Value", "Description"}));
            for (const Scalar& s : scalars) {
                DocNode r = row({cell({codeInline(s.name)}), cell({codeInline(s.value)}), cell(inlineFromText(site, s.doc))});
                table.children.push_back(withAttr(r, "id", scriptAnchor(s.name)));
            }
            root.children.push_back(withAttr(elem("section", {std::move(table)}), "title", "Values"));
        }
        for (DocNode& section : sections) {
            root.children.push_back(std::move(section));
        }

        if (!validateDocPage(root, error)) {
            error = data.file.generic_string() + ": " + error;
            return false;
        }
        Page page;
        page.id = id;
        page.title = data.path;
        const std::size_t slash = data.path.rfind('/');
        page.navTitle = slash == std::string::npos ? data.path : data.path.substr(slash + 1);
        page.root = std::move(root);
        page.source = data.file;
        page.generated = true;
        site.pages.push_back(std::move(page));
    }
    return true;
}

}
