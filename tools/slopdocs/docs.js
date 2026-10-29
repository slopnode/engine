(function () {
    var input = document.getElementById("sd-search-input");
    var results = document.getElementById("sd-search-results");
    if (!input || !results) {
        return;
    }
    var rel = document.body.getAttribute("data-rel") || "";
    var active = -1;

    function entries() {
        return window.SD_INDEX || [];
    }

    function score(entry, query) {
        var title = entry.t.toLowerCase();
        if (title === query) {
            return 0;
        }
        if (title.indexOf(query) === 0) {
            return 1;
        }
        if (title.indexOf(query) >= 0) {
            return 2;
        }
        if (entry.p && entry.p.toLowerCase().indexOf(query) >= 0) {
            return 3;
        }
        return -1;
    }

    function clear() {
        results.innerHTML = "";
        results.hidden = true;
        active = -1;
    }

    function render(query) {
        var q = query.trim().toLowerCase();
        if (!q) {
            clear();
            return;
        }
        var matches = [];
        entries().forEach(function (entry) {
            var s = score(entry, q);
            if (s >= 0) {
                matches.push({ s: s, e: entry });
            }
        });
        matches.sort(function (a, b) {
            return a.s - b.s || a.e.t.localeCompare(b.e.t);
        });
        matches = matches.slice(0, 20);
        results.innerHTML = "";
        if (!matches.length) {
            var none = document.createElement("li");
            none.className = "sd-no-results";
            none.textContent = "No matches";
            results.appendChild(none);
        }
        matches.forEach(function (m) {
            var li = document.createElement("li");
            var a = document.createElement("a");
            a.href = rel + m.e.u;
            a.textContent = m.e.t;
            if (m.e.p) {
                var span = document.createElement("span");
                span.className = "sd-result-page";
                span.textContent = m.e.p;
                a.appendChild(span);
            }
            li.appendChild(a);
            results.appendChild(li);
        });
        results.hidden = false;
        active = -1;
    }

    function move(delta) {
        var items = results.querySelectorAll("li");
        if (!items.length) {
            return;
        }
        if (active >= 0) {
            items[active].classList.remove("sd-active");
        }
        active = (active + delta + items.length) % items.length;
        items[active].classList.add("sd-active");
        items[active].scrollIntoView({ block: "nearest" });
    }

    input.addEventListener("input", function () {
        render(input.value);
    });
    input.addEventListener("keydown", function (event) {
        if (event.key === "Escape") {
            clear();
            input.blur();
        } else if (event.key === "ArrowDown") {
            event.preventDefault();
            move(1);
        } else if (event.key === "ArrowUp") {
            event.preventDefault();
            move(-1);
        } else if (event.key === "Enter") {
            var items = results.querySelectorAll("li a");
            var target = active >= 0 ? items[active] : items[0];
            if (target) {
                window.location.href = target.href;
            }
        }
    });
    document.addEventListener("click", function (event) {
        if (!results.contains(event.target) && event.target !== input) {
            clear();
        }
    });
    document.addEventListener("keydown", function (event) {
        if (event.key === "/" && document.activeElement !== input) {
            event.preventDefault();
            input.focus();
        }
    });
})();
