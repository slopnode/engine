if exists("b:current_syntax")
  finish
endif

syntax case match

syntax match slopengineComment ";.*$"
syntax region slopengineComment start="#|" end="|#"
syntax match slopengineComment "#;"

syntax region slopengineString start=/"/ skip=/\\./ end=/"/ contains=slopengineEscape

syntax match slopengineEscape /\\./ contained

syntax match slopengineCharacter "#\\\([a-zA-Z][a-zA-Z0-9-]*\|.\)"

syntax match slopengineBoolean "#\(t\%(rue\)\?\|f\%(alse\)\?\|unspecified\|eof\|default\|<unspecified>\|<eof>\)\>"

syntax match slopengineNumber "\v<(\#[xXoObBeEiI])*\-?(\d+\/\d+|\d+\.\d+|\.\d+|\d+)([eE][+-]?\d+)?>"

syntax match slopengineQuote "'\|`\|,@\|,"

syntax match slopengineKeyword "\(^\|[( \t]\)\@<=\(else\|=>\|\.\.\.\)\([) \t]\|$\)\@="

syntax match slopengineSpecialVariable "\*[-A-Za-z0-9_./!?+<>=]\+\*"

syntax match slopengineParen "[()]"

syntax match slopengineHeadFunction "\%((\)\@<=[-A-Za-z0-9_./!?+*<>=]\+"

syntax match slopengineHeadBuiltin "\%((\)\@<=\V\(car\|cdr\|caar\|cadr\|cdar\|cddr\|caddr\|cdddr\|cons\|cons*\|list\|list?\|list-ref\|list-tail\|append\|append!\|reverse\|reverse!\|length\|map\|filter\|reduce\|apply\|assoc\|assq\|assv\|member\|memq\|memv\|eq?\|eqv?\|equal?\|null?\|pair?\|number?\|integer?\|real?\|rational?\|string?\|symbol?\|procedure?\|boolean?\|vector?\|char?\|zero?\|positive?\|negative?\|odd?\|even?\|not\|min\|max\|abs\|floor\|ceiling\|round\|truncate\|sqrt\|expt\|modulo\|remainder\|quotient\|gcd\|lcm\|sin\|cos\|tan\|asin\|acos\|atan\|exp\|log\|format\|display\|display*\|write\|write-string\|newline\|read\|read-line\|string\|string-length\|string-append\|string-ref\|string-set!\|substring\|string=?\|string<?\|string>?\|string<=?\|string>=?\|string-ci=?\|string->symbol\|symbol->string\|string->number\|number->string\|string->list\|list->string\|string-upcase\|string-downcase\|char->integer\|integer->char\|vector\|vector-ref\|vector-set!\|vector-length\|make-vector\|vector->list\|list->vector\|vector-fill!\|vector-map\|vector-for-each\|for-each\|hash-table\|hash-table*\|make-hash-table\|hash-table-ref\|hash-table-set!\|hash-table->list\|sort\|sort!\|error\|exit\|eval\|eval-string\|load\|values\|dynamic-wind\|1+\|1-\|gensym\|copy\|fill!\)\m\(\%([ \t)]\)\|$\)\@="

syntax match slopengineHeadKeyword "\%((\)\@<=\V\(define\|define*\|define-macro\|define-macro*\|define-bacro\|define-bacro*\|define-constant\|define-expansion\|define-syntax\|define-record-type\|define-module\|define-class\|define-animal\|define-values\|lambda\|lambda*\|dilambda\|let\|let*\|let-values\|let*-values\|letrec\|letrec*\|named-lambda\|do\|if\|cond\|case\|when\|unless\|and\|or\|begin\|set!\|quote\|quasiquote\|unquote\|unquote-splicing\|delay\|delay-force\|force\|dynamic-wind\|call/cc\|call-with-current-continuation\|call-with-values\|call-with-exit\|with-let\|with-baffle\|catch\|syntax-rules\)\m\(\%([ \t)]\)\|$\)\@="

" Engine-invoked hooks: fixed names from docs/scripting-api.md's Hooks
" section, plus the on-action-{id}/on-use-{name} (and, by codebase
" convention, on-enter-/on-exit-/on-touch-) prefixed callback names.
syntax match slopengineHook "\%((\)\@<=\V\(prepare-first-person\|on-map-ready\|on-startup\|draw-file-menu\|draw-pause-menu\|draw-debug-menu\|draw-modals\|tick\|draw-hud\|draw-title\|on-sprite-hint\|on-sight\|sight-filter\)\m\(\%([ \t)]\)\|$\)\@="
syntax match slopengineHook "\%((\)\@<=\V\(on-action-\|on-use-\|on-enter-\|on-exit-\|on-touch-\)\m[-A-Za-z0-9_./!?+*<>=]\+"

highlight default link slopengineComment Comment
highlight default link slopengineString String
highlight default link slopengineEscape SpecialChar
highlight default link slopengineCharacter Character
highlight default link slopengineBoolean Boolean
highlight default link slopengineNumber Number
highlight default link slopengineQuote Operator
highlight default link slopengineKeyword Keyword
highlight default link slopengineSpecialVariable Constant
highlight default link slopengineParen Delimiter
highlight default link slopengineHeadFunction Function
highlight default link slopengineHeadBuiltin Function
highlight default link slopengineHeadKeyword Keyword
highlight default link slopengineHook Special

let b:current_syntax = "slopengine"
