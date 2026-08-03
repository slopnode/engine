@page tut_scheme Introduction to Scheme (s7)

# History of Scheme & Lisp

I won't dive into the entire background of Lisp and Scheme because it's a very long and complicated history. If it's something you find interesting it's definitely worth reading more about. I'll just cover enough to explain why Scheme looks the way it does.

Back in the late 1950s the dominant programming language was Fortran. It was great for numerical and mathematical calculations, but according to John McCarthy at MIT it wasn't very useful for working with symbolic data, logic, or the kinds of data structures needed for artificial intelligence research. To solve this problem he designed the List Processor, or Lisp, which focused on symbolic expressions rather than purely numerical computation.

Throughout the 1960s Lisp became popular in AI research, with people like Marvin Minsky and Seymour Papert using it extensively at MIT. As the language spread, different groups started creating their own dialects and implementations, each with their own extensions and ideas. By the 1970s there were many incompatible versions of Lisp, making it difficult to share programs and ideas between systems.

Around the mid-1970s Gerald Jay Sussman and Guy L. Steele Jr. began a programming language design experiment to see what features were actually fundamental to a programming language. While studying Alonzo Church's lambda calculus from the 1930s they realized that much of Lisp's complexity could be expressed with only a small set of primitives centered around functions. Instead of building a language with every feature imaginable, they designed Scheme with a very small core and encouraged programmers to build their own abstractions.

Today Common Lisp, Clojure, and Racket are some of the more popular descendants of the Lisp family. Racket, being a dialect of Scheme, is a multi-paradigm general-purpose language that lets you either build your own language or use existing ones that provide conveniences like objects, interfaces, structures, and much more. It even ships with an IDE capable of things like 2D and 3D graphics.

- [Recursive Functions Of Symbolic Expression And Their Computation By Machine - 1960](https://www-formal.stanford.edu/jmc/recursive.html)
- [SCHEME An Interpreter For Extended Lambda Calculus - 1975](https://research.scheme.org/lambda-papers/lambda-papers-scheme-report.html)
- [Scheme Registry](https://registry.scheme.org/)

## s7 Scheme

To be honest I haven't dug into the history, the authors, or the reason this language was created. It was only until I started writing this document that I decided to read into it.

The language s7 was created by Bill Schottstaedt originally as an embedded language for his sound editor, Snd. The goal was to provide a lightweight Scheme implementation that could be easily integrated into applications while still providing the expressive power of Scheme. Unlike larger Scheme implementations that are intended to be complete standalone programming environments, s7 focuses on being small, portable, and practical to embed inside other software.

Because of this background, s7 fits particularly well as a scripting language for an engine. It provides enough of the Scheme language to allow complex behavior and user-defined abstractions while keeping the runtime and integration layer relatively simple. The engine can expose only the functionality that is necessary, while package authors can use Scheme itself to create the higher-level systems they need.

Considering this now, the choice for s7 Scheme in this engine might make a little more sense. Just as Scheme was designed around a minimal core where developers build their own abstractions, this engine also provides only a handful of fundamental components. Package authors are expected to build higher-level abstractions that best fit their own applications.

 - [s7 Website](https://ccrma.stanford.edu/software/s7/s7.html)

## Functional programming

Developers coming from C, C++, Java, or C# are often used to describing software in terms of objects with associated methods and state. Functional programming instead focuses on describing behavior as functions that transform data. Rather than building large object hierarchies, data is passed through chains of functions that each perform a specific operation.

This doesn't mean data structures disappear. Instead, the emphasis shifts away from attaching behavior to rigid object types and toward composing small reusable functions. In practice you'll still organize data into meaningful structures, but behavior is kept separate and combined where needed.

This style fits Scheme well. Functions are treated as first-class values, can be passed around like any other object, and make it easy to build higher-level abstractions from a very small language core.

## Engine architecture

The engine itself follows a similar philosophy. Internally it uses flecs and an Entity Component System (ECS) to represent game objects. Instead of describing objects with inheritance ("is a"), entities are composed from components ("has a"). Systems then operate on every entity containing the components they care about.

The interface between the engine and packages is intentionally minimal. Rather than exposing large object hierarchies, the engine provides callback hooks and prepared data structures. Game objects, referred to as things, are either built-in engine types or compositions of those types.

Callbacks can be defined on a composite type, overridden for individual thing instances, or even replaced at run time to change behavior dynamically. As long as a callback implements the expected signature, package authors are free to build whatever abstractions best suit their application.

# Features

## REPL

Before integrating Scheme into the engine it's often useful to experiment interactively. The engine includes a small REPL called `sloprepl` for exactly this purpose.

REPL stands for read-eval-print loop and you see this commonly with languages like JavaScript or Python. By default the program starts up just an interactive prompt. You can with the argument `--load` provide a file to prepare the REPL environment, or `--exec` to run the file and exit.

## Symbolic expressions

At the core of Scheme are what people call symbolic expressions, or s-expressions (often shortened to s-exprs). An s-expression is the notation Scheme uses to represent both data and code. Unlike languages such as C or Java where code and data have different syntax, Scheme uses the same notation for everything.

An s-expression can be an atomic value such as an integer, float, string, boolean, or symbol.

<pre><code class="language-scheme">#t
#f
1
1.0
"test"
'symbol
</code></pre>

Or it can be a list containing other expressions.

<pre><code class="language-scheme">(+ 1 2)
(list 1 2 3)
(display "Hello")
</code></pre>

Because every expression follows the same structure, Scheme doesn't need separate syntax for function calls, operators, or many language constructs. The first element of a list is treated as the operation to perform, while the remaining elements become its arguments.

<pre><code class="language-scheme">(+ 1 2)
(+ (+ 2 4) 2)
(+ (+ 2 4) (+ 3 5))
</code></pre>

You might hear this described as Reverse Polish notation, although it's more accurately called prefix notation. Every expression starts with the operation followed by its operands. Since every nested expression is already grouped, order of operations is always explicit.

<pre><code class="language-scheme">(<operator> <operand> ...)
(<operator> (<operator> <operand> ...) ...)
(<operator> (<operator> <operand> ...)
            (<operator> <operand> ...))
</code></pre>

The engine also uses symbolic expressions to describe package assets such as sprites, geometry, maps, and other resources. This keeps the majority of assets in a single consistent format that can be edited with any text editor without requiring additional parsers. Although these files are treated as data rather than executable Scheme, they are still valid symbolic expressions and are checked for proper structure.


## Variables

Variables in s7 are dynamically typed. They can be defined via the keyword `define`. Variable (and function) names can use symbols and Unicode characters for variable names.

<pre><code class="language-scheme">(define my-number 1)
(define my-string "string")
(define my-float 1.258)
(define my-bool #t)
</code></pre>

A convention in Scheme is if a function returns a boolean value the name will have a question mark on the name. For type checking:

<pre><code class="language-scheme">(string? "Hello") ; #t
(string? 100)     ; #f

(number? 100)     ; #t
(number? 1.0)     ; #t

(integer? 100)    ; #t
(integer? 100.0)  ; #f

(float? 100)      ; #f
(float? 1.0)      ; #t

(list? '(1 2 3))  ; #t
(list? 10)        ; #f

(null? '())       ; #t
(null? 10)        ; #f
</code></pre>

## Lists

Lists are special in Scheme because behind the scenes everything is a list. You will commonly see the shorthand notation `'(...)` for `(list ...)`. The convention for `null` in Scheme is also an empty list.

<pre><code class="language-scheme">(define my-list '(1 2 3 4))
(define other-list (list 1 2 3 4)) ; also ok
</code></pre>

Scheme doesn't have built-in concepts of classes, interfaces, or structures. Developers create lists and give structure to it by using parameterization. 

<pre><code class="language-scheme">(define (create-car brand type color)
    `(,brand ,type ,color))
</code></pre>

What will be odd for developers of other languages are the function names scheme uses for reading and manipulating lists, `cons`, `car`, `cdr`, and then `cadr` and so forth.

<pre><code class="language-scheme">; A pair of (A, B)
(define a-pair (cons 1 2))
a-pair ; (1 . 2)
(car a-pair) ; 1
(cdr a-pair) ; 2

; A list
(define a-list '(1 2 3 4))

; How scheme represents an list, pairs terminated with '()
(cons 1 (cons 2 (cons 3 (cons 4 '())))) ; (1 2 3 4)

; car/cdr operations to read list
(car a-list)  ; 1
(cdr a-list)  ; (2 3 4)
(cadr a-list) ; 2
(cddr a-list) ; (3 4)

(cdar a-list) ; error
;cdar argument, (1 2 3 4), is a pair but should be a list whose car is also a list

(cdar '('(1 2 3) 3 4 5)) ; ((1 2 3)) what?
</code></pre>

The reason that these names stuck around is due to historical reasons that these functions mapped directly to machine instructions at the time, called CAR and CDR.

* CAR → Contents of the Address Register
* CDR → Contents of the Decrement Register

Even though this might not reflect what is happening, the names have managed to stick around for decades now where `car` more or less just means the first element of a pair or less and `cdr` means the 2nd or rest of the list. As obtuse as this feels at first, it is a nice construction for creating recursive loops as you have the current element with `car`, and then the function simply accepts the rest of the list from `cdr` until `'()` has been found to terminate the list. 

Dialects and implementations of scheme today often implement macros like `first`, `second`, or `list-ref` as convience functions. This might make its way into the engine to make the scripts easier to read and write. 

## Conditionals

If conditions are similar to other languages. 

<pre><code class="language-scheme">; if condition, (if <statement> <when true> <when false>)
(display (if (eq? 2 2) #t #f)) ; #t
(display (if (eq? 2 4) #t #f)) ; #f
</code></pre>

For situations where a condition check is needed, but that you don't want to program both a #t and #f path, there is the statement `when` available.

<pre><code class="language-scheme">; when true, do this
(when (eq? 2 2) (display "equal")) ; "equal"
(when (eq? 2 3) (display "equal")) ; won't be executed
</code></pre>

For statements with multiple conditions (if/elseif/else) Scheme includes the `cond` statement

<pre><code class="language-scheme">; conditional trees
(define foo 100)
(cond
    ((eq? foo 90) "Equals 90")
    ((eq? foo 80) "Equals 80")
    (else "Does not equal 90 or 80"))
</code></pre>

`and`, `or` and `not` as logical operators are available as well

<pre><code class="language-scheme">(and (eq? 1 1) (eq? 1 1)) ; #t
(and (eq? 1 2) (eq? 1 1)) ; #f

(or (eq? 1 1) (eq? 1 1)) ; #t
(or (eq? 1 2) (eq? 1 1)) ; #t

(not (eq? 1 1)) ; #f
</code></pre>

`xor`, or exclusive or you implement yourself if needed.

<pre><code class="language-scheme">; As function
(define (xor a b)
  (or (and a (not b))
      (and (not a) b)))

; As macro
(define-macro (xor a b)
  `(or (and ,a (not ,b))
       (and (not ,a) ,b)))
</code></pre>

## Functions

Functions are also defined via `define` or as `lambda`, where like other languages the function is anonymous.  

<pre><code class="language-scheme">; Define and call
(define (add-numbers num1 num2)
    (+ num1 num2))
(add-numbers 4 2) ; 6

; Goofy, but just as an example
((lambda (num1 num2) (+ num1 num2)) 4 2) ; 6
</code></pre>

When defining functions in Scheme that mutate data it is a convention to append `!` to the name. For example `set!`.

<pre><code class="language-scheme">(define my-var 10)
(set! my-var 20) ; Changes my-var to 20
</code></pre>

## Higher order functions

Higher order functions are functions that take other functions as arguments or return functions. 

<pre><code class="language-scheme">; Function that takes another function as argument
(define (apply-twice f x)
  (f (f x)))

(apply-twice (lambda (x) (+ x 1)) 5)
; Returns: 7

; Function that returns another function
(define (make-adder n)
  (lambda (x) (+ x n)))

(define add-five (make-adder 5))
(add-five 3)
; Returns: 8
</code></pre>

## Iteration

Scheme doesn't have traditional for loops like C or Java. Instead, it uses recursion and built-in iteration functions.

<pre><code class="language-scheme">; Using built-in functions for iteration
(map (lambda (x) (* x x)) '(1 2 3 4))
; Returns: (1 4 9 16)

(append '(1 2) '(3 4))
; Returns: (1 2 3 4)

(for-each (lambda (x) (display x)) '(1 2 3))
; Displays: 123
</code></pre>

For explicit recursion, you can define recursive functions:

<pre><code class="language-scheme">(define (factorial n)
  (if (= n 0)
      1
      (* n (factorial (- n 1)))))

(factorial 5) ; Returns: 120
</code></pre>

## Local Bindings

Local bindings in Scheme are created using `let`, `let*`, and `define` constructs. Each has different scoping rules and use cases:

### `let` bindings
<pre><code class="language-scheme">(let ((x 1) (y 2))
  (+ x y)) ; Returns: 3
</code></pre>

`let` creates bindings that are all available simultaneously within the body. Variables defined in the binding list cannot reference each other. They are evaluated before any of the bindings take effect.

### `let*` bindings
<pre><code class="language-scheme">(let* ((x 1) (y (+ x 1)))
  (+ x y)) ; Returns: 3
</code></pre>

`let*` creates bindings sequentially, where each binding can reference previous bindings in the same list. 

### `define` bindings
<pre><code class="language-scheme">(define (my-function)
  (let ((x 1) (y 2))
    (+ x y))) ; Returns: 3
</code></pre>

`define` is used to create global bindings or local bindings within function scopes. It's typically used for defining functions and creating top-level variables.

The choice between these constructs depends on your specific needs:
- Use `let` when you need parallel binding where each variable's value doesn't depend on others
- Use `let*` when you need sequential binding where later bindings can reference earlier ones
- Use `define` for creating functions or when you want the binding to be available in the current scope

## Macros

Macros let you extend the language itself by generating Scheme code before it is evaluated. Unlike C/C++ macros that are expanded at compile time, macros in scheme are expanded at run-time first in the definition environment and then evaluated. Because everything is an s-expr and this is evaluated during run-time it is possible to create code that can change itself based on data expression on the fly.

<pre><code class="language-scheme">; Simple macro example
(define-macro (when* condition body)
  `(if ,condition (begin ,body) #f))

(when* (> 5 3) (display "Five is greater than three"))
; Displays: Five is greater than three
</code></pre>