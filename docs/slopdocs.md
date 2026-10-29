@page slopdocs slopdocs

slopdocs turns one package into a static HTML site. It reads the package's `docs/` folder, where pages are written as s-expressions, and writes plain HTML with a sidebar, a per-page table of contents, search, and syntax-highlighted Scheme. Later phases add reference sections generated from the package's `data/` catalogs and the docstrings in its `scripts/`.

# Running

```
slopdocs <package-path> --out <dir> [--verbose]
```

`<package-path>` is a directory containing `package.meta`. The site is written into `--out`. A package with no `docs/` folder still produces an index page from `package.meta`. The exit code is non-zero when a page fails to evaluate, a node is misused, `docs.meta` cannot be parsed, or a `ref` or `page-ref` does not resolve. Errors name the file and line.

# Layout

```
<package>/
  package.meta
  docs/
    docs.meta
    index.s7
    modding.s7
    guides/
      weapons.s7
    lib.s7
    custom.css
    images/
```

Every `.s7` file under `docs/` is evaluated. A file registers pages with `page`; anything else it defines is available to the pages that load it with `require-doc`. Files are evaluated in path order, and `require-doc` loads a file early when a page depends on it. The `images/` folder is copied into the output unchanged.

# docs.meta

<pre><code class="language-scheme">(docs
  (title "Slopdoom")
  (nav "index" "modding" ("Guides" "guides/weapons" "guides/monsters"))
  (stylesheet "custom.css")
  (engine-docs "https://pages.slopnode.net/engine-docs/"))
</code></pre>

`title` names the site, defaulting to the package name. `nav` orders the sidebar by page id, with a list whose first element is a string forming a labelled group. Pages not listed are appended alphabetically, and `index` always comes first. `stylesheet` lists files under `docs/` that are copied next to the built-in theme and linked after it. `engine-docs` is the base URL of the engine documentation. With it set, engine function and hook names in `ref` nodes and in `scheme` code blocks link to their entries in the scripting API reference; without it they are only highlighted.

# Pages

A page is a Scheme file evaluated with the slopdocs prelude loaded. The prelude defines constructors that return tagged lists, so a page is a tree of lists. The same tree can be written as quoted data, which the writer treats identically.

<pre><code class="language-scheme">(require-doc "lib")

(page "modding" "Modding Slopdoom"
  (p "A mod is a package mounted on top of Slopdoom with " (code "--mod") ".")

  (section "Behaviors"
    (p "Behaviors are registered by name. See " (ref "thing-type") ".")
    (code 'scheme "
      (behavior-register! \"lunge\"
        '((cooldown . 1.5)
          (range . 6)))"))

  (section :id "weapons" "Weapons"
    (weapon-table))

  (note "Mods run with the mod role."))
</code></pre>

`page` takes an id, a title, and block nodes. The id is the output path without `.html`, so `"guides/weapons"` becomes `guides/weapons.html`, and it is what `nav` and `page-ref` refer to.

`weapon-table` above is not part of the vocabulary. It is a function the author wrote in `docs/lib.s7`:

<pre><code class="language-scheme">(define *weapons*
  '((1 "fist" "none")
    (2 "pistol" "bullets")))

(define (weapon-table)
  (table :class "weapons"
    (header "Slot" "Weapon" "Ammo")
    (map (lambda (w)
           (row (number->string (car w)) (code (cadr w)) (caddr w)))
         *weapons*)))
</code></pre>

A list of nodes passed where a node is expected is spliced in, which is what lets `map` produce rows. `#f` and `()` are dropped, so `(if cond (p "x") #f)` works inside a tree.

## Attributes

Every node takes leading keyword attributes before its content. `:id` sets the element id and `:class` adds classes.

<pre><code class="language-scheme">(section :id "install" "Installing" ...)
(table :class "compact" ...)
(p :class "fine-print" "Small text.")
</code></pre>

## Block nodes

Node|Form
-|-
`section`|`(section "Title" block...)`. Sections nest and become h2 to h6. The anchor is slugged from the title unless `:id` is given.
`p`|`(p inline...)`
`code`|`(code 'scheme "text")` or `(code "text")`. Common leading indentation is removed. Inline when used inside inline content.
`ul`, `ol`|`(ul entry...)`. Each entry is inline content or `(item block...)`.
`table`|`(table (header cell...) (row cell...) ...)`. A cell is inline content or `(cell inline...)` when it has several parts.
`dl`|`(dl (term "name" inline...) ...)`
`note`, `tip`, `warning`|`(note inline...)` or `(note block...)`. Inline content becomes one paragraph.
`image`|`(image "images/x.png" "alt" caption...)`. Paths are relative to `docs/`.
`quote-block`|`(quote-block inline...)` or `(quote-block block...)`
`hr`|`(hr)`
`html`|`(html "raw markup")`. Also usable inline.

## Inline nodes

Node|Form
-|-
strings|Plain text. Newlines and the indentation after them collapse to one space.
`em`, `strong`, `kbd`|`(em inline...)`
`code`|`(code "text")`
`link`|`(link "url" inline...)`. Without content the url is shown.
`page-ref`|`(page-ref "id" inline...)`. Without content the page title is shown.
`ref`|`(ref "name")`. Links a symbol. An unresolved name is an error.
`br`|`(br)`

Block nodes inside inline content, and bare text inside a section, are errors.

# Styling

The output carries a stable class contract so a stylesheet can restyle anything:

- Every element has a class named after its node with the `sd-` prefix: `sd-section`, `sd-p`, `sd-code`, `sd-table`, `sd-note`, `sd-image`, and so on. Sections also carry `sd-section-1` to `sd-section-6` for their depth. Code blocks add `sd-code-block` and `sd-lang-scheme`.
- The shell uses fixed ids: `sd-title`, `sd-search`, `sd-nav`, `sd-toc`, `sd-content`.
- Scheme tokens are `sd-tok-comment`, `sd-tok-string`, `sd-tok-number`, `sd-tok-boolean`, `sd-tok-char`, `sd-tok-keyword`, `sd-tok-special`, `sd-tok-call`, `sd-tok-symbol`, `sd-tok-paren`, `sd-tok-quote`, and a linked symbol is wrapped in `a.sd-tok-link`.
- Colors, fonts, spacing and the content width are custom properties on `:root` in the built-in `style.css`, with a dark set under `prefers-color-scheme: dark`. Changing the look of the whole site is overriding a handful of variables.

A `:class` attribute is added alongside the `sd-` classes, so a custom rule only has to add what differs:

```
.sd-table.weapons td:first-child {
    font-weight: 600;
}

:root {
    --sd-accent: #7a3f9a;
    --sd-content-width: 64rem;
}
```

# Output

```
<out>/
  index.html
  <page>.html
  guides/<page>.html
  style.css
  custom.css
  docs.js
  search-index.js
  images/
```

Pages in subfolders link back with relative paths, so the site works from `file://` as well as from a web server. Nothing is fetched from a CDN. Search is a client-side filter over page and section titles.

# Sandbox

Pages run in the same hardened s7 heap the engine uses for data files: file access, `load`, `eval`, `system` and the like are disabled. `require-doc` is the only way to pull in another file, and it only reaches `docs/`.
