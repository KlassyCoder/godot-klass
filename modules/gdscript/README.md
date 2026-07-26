# Basic GDScript module architecture
This provides some basic information in how GDScript is implemented and integrates with the rest of the engine. You can learn more about GDScript in the [documentation](https://docs.godotengine.org/en/latest/tutorials/scripting/gdscript/index.html). It describes the syntax and user facing systems and concepts, and can be used as a reference for what user expectations are.

For general language design guidelines, please visit the [contributing docs](https://contributing.godotengine.org/en/latest/engine/guidelines/gdscript_language_guidelines.html).

## Integration into Godot

GDScript is integrated into Godot as a module. Since modules are optional, this means that Godot may be built without GDScript and work perfectly fine without it!

The GDScript module interfaces with Godot's codebase by inheriting from the engine's scripting-related classes. New languages inherit from [`ScriptLanguage`](/core/object/script_language.h), and are registered in Godot's [`ScriptServer`](/core/object/script_language.h). Scripts, referring to a file containing code, are represented in the engine by the `Script` class. Instances of that script, which are used at runtime when actually executing the code, inherit from [`ScriptInstance`](/core/object/script_instance.h).

To access Godot's internal classes, GDScript uses [`ClassDB`](/core/object/class_db.h). `ClassDB` is where Godot registers classes, methods and properties that it wants exposed to its scripting system. This is how GDScript understands that `Node2D` is a class it can use, and that it has a `get_parent()` method.

[Built-in GDScript methods](https://docs.godotengine.org/en/latest/classes/class_@gdscript.html#methods) are defined and exported by [`GDScriptUtilityFunctions`](gdscript_utility_functions.h), whereas [global scope methods](https://docs.godotengine.org/en/latest/classes/class_%2540globalscope.html) are registered in [`Variant::_register_variant_utility_functions()`](/core/variant/variant_utility.cpp).


## Compilation

Scripts can be at different stages of compilation. The process isn't entirely linear, but consists of this general order: tokenizing, parsing, analyzing, and finally compiling. This process is the same for scripts in the editor and scripts in an exported game. Scripts are stored as text files in both cases, and the compilation process must happen in full before the bytecode can be passed to the virtual machine and run.

The main class of the GDScript module is the [`GDScript`](gdscript.h) class, which represents a class defined in GDScript. Each `.gd` file is called a _class file_ because it implicitly defines a class in GDScript, and thus results in an associated `GDScript` object. However, GDScript classes may define [_inner classes_](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_basics.html#inner-classes), and those are also represented by further `GDScript` objects, even though they are not in files of their own.

The `GDScript` class contains all the information related to the corresponding GDScript class: its name and path, its members like variables, functions, symbols, signals, implicit methods like initializers, etc. This is the main class that the compilation step deals with.

A secondary class is `GDScriptInstance`, defined in the same file, containing _runtime_ information for an instance of a `GDScript`, and is more related to the execution of a script by the virtual machine.


### Loading source code

This mostly happens by calling `GDScript::load_source_code()` on a `GDScript` object. Parsing only requires a `String`, so it is entirely possible to parse a script without a `GDScript` object!


### Tokenizing (see [`GDScriptTokenizer`](gdscript_tokenizer.h))

Tokenizing is the process of converting the source code `String` into a sequence of tokens, which represent language constructs (such as `for` or `if`), identifiers, literals, etc. This happens almost exclusively during the parsing process, which asks for the next token in order to make sense of the source code. The tokenizer is only used outside of the parsing process in very rare exceptions.


#### Conditional compilation (see [`GDScriptConditionalCompilation`](gdscript_conditional_compilation.h))

GDScript supports Swift-style conditional compilation directives, resolved entirely inside `GDScriptTokenizerText` before the parser ever sees a token. An excluded branch is skipped line-by-line at the tokenizer level, so it never gets parsed, analyzed, compiled, or shipped in a `.gdc` export; it doesn't even need to be syntactically valid GDScript.

```gdscript
func _ready() -> void:
	#@if DEBUG
	print("debug only")
	#@elif MOBILE
	print("mobile")
	#@else
	print("other")
	#@endif
```

Grammar:

```
directive     := WS* '#@' keyword body? WS* ( '#' any* )? EOL
keyword       := 'if' | 'elif' | 'else' | 'endif'
                 -- 'if'/'elif' require a non-empty body; 'else'/'endif' require an empty body.
condition     := or_expr
or_expr       := and_expr ( ('||' | 'or')  and_expr )*
and_expr      := unary   ( ('&&' | 'and') unary   )*
unary         := ('!' | 'not') unary | primary
primary       := 'true' | 'false' | IDENTIFIER | '(' or_expr ')'
IDENTIFIER    := [A-Za-z_][A-Za-z0-9_]*
```

Precedence is `!` > `&&` > `||`. There is no `#define`, `#undef`, `#include`, `#error`, `#ifdef`/`#ifndef`, macros with arguments, comparisons, numeric literals, arithmetic, or `defined()`. An undefined identifier evaluates to `false`, without a warning or error (matching the C preprocessor and Swift). Two independent nesting limits are enforced, both fixed at 32 and both hard errors past the limit: parenthesized subexpressions within a single condition (`CONDITION_MAX_DEPTH`), and `#@if` directives nested inside other `#@if` directives (`conditional_stack` depth).

Note that only *flag identifiers* are case-insensitive; the grammar's own words are not. `true`, `false`, `and`, `or` and `not` are recognized in lowercase only, so `#@if TRUE` is not the literal `true`: it parses as an ordinary identifier, and unless a flag named `true` is declared it evaluates to `false`. For the same reason `#@if a && or` treats the trailing `or` as an operand rather than reporting a syntax error. Write the operators in lowercase.

**Recognition rule.** A source line is a directive line iff (a) the first non-whitespace characters on the line are `#` immediately followed by `@` (no space between them), and (b) it is not inside an open `(`/`[`/`{` and is not a `\`-continuation line. A directive line is valid iff the maximal run of ASCII letters immediately after `#@` is exactly one of `if`, `elif`, `else`, `endif`; any other letter run, including the empty run, is a hard error (e.g. `#@ifdef`, `#@elseif`, or bare `#@`) rather than silently falling back to being treated as a comment. `#@` has no other meaning in GDScript, so this is zero-risk: no existing comment can accidentally become a directive.

Mid-line `#@if` is a plain comment, silently: `x = 1 #@if FOO` is not a directive line because the `#@` doesn't start the line, so it is never an error. A directive is also indentation-transparent and does not open a block on its own:

```gdscript
# NOT VALID: body is indented but nothing opened a block.
#@if DEBUG
    print("debug only")
#@endif

# Directive column is irrelevant; this is equally valid:
func _process(d: float) -> void:
#@if DEBUG
	print("tick")
#@endif
	pass
```

**Flags.** Flag identifiers are matched case-insensitively. A flag is `true` if it is listed in the `gdscript/conditional_compilation/flags` project setting (a `PackedStringArray`, supporting the `.feature` override suffix), or if `OS::has_feature()` returns `true` for it; that already covers platform, build target, architecture, and export preset custom feature tags. Editor, `.gdc` export, and runtime all resolve flags through this same mechanism, so they stay consistent by construction. Export additionally installs a closed-world override built from `EditorExportPlatform::get_features()`, so the baked `.gdc` reflects exactly the target being exported for.

Editing the `gdscript/conditional_compilation/flags` project setting in the editor is a live change: `register_types.cpp` connects to `ProjectSettings`'s `settings_changed` signal, invalidates `GDScriptConditionalCompilation`'s cached flag set, and, if the resolved flags actually changed, clears `GDScriptCache` and calls `GDScriptLanguage::reload_all_scripts()` so open/loaded scripts get re-tokenized against the new flags.

**Caveat:** scripts exported as text (`EditorExportPreset::MODE_SCRIPT_TEXT`) and scenes with built-in scripts ship with their full source, including excluded branches; only binary token export (the default) actually removes excluded source from the package. Prefer binary token export if conditional compilation is being used to keep code out of a shipped build.


### Parsing (see [`GDScriptParser`](gdscript_parser.h))

The parser takes a sequence of tokens and builds [the abstract syntax tree (AST)](https://en.wikipedia.org/wiki/Abstract_syntax_tree) of the GDScript program. The AST is used in the analyzing and compilation steps, and the source code `String` and sequence of tokens are discarded. The AST-building process finds syntax errors in a GDScript program and reports them to the user.

The parser class also defines all the possible nodes of the AST as subtypes of `GDScriptParser::Node`, not to be confused with Godot's scene tree `Node`. For example, `GDScriptParser::IfNode` has two children nodes, one for the code in the `if` block, and one for the code in the `else` block. A `GDScriptParser::FunctionNode` contains children nodes for its name, parameters, return type, body, etc. The parser also defines typechecking data structures like `GDScriptParser::Datatype`.

The parser was [intentionally designed](https://godotengine.org/article/gdscript-progress-report-writing-new-parser/#less-lookahead) with a look-ahead of a single token. This means that the parser only has access to the current token and the previous token (or, if you prefer, the current token and the next token). This parsing limitation ensures that GDScript will remain syntactically simple and accessible, and that the parsing process cannot become overly complex.


### Analysis and typechecking (see [`GDScriptAnalyzer`](gdscript_analyzer.h))

The analyzer takes in the AST of a program and verifies that "everything checks out". For example, when analyzing a method call with three parameters, it will check whether the function definition also contains three parameters. If the code is typed, it will check that argument and parameter types are compatible.

There are two types of functions in the analyzer: `reduce` functions and `resolve` functions. Their parameters always include the AST node that they are attempting to reduce or resolve.
- The `reduce` functions work on GDScript expressions, which return values, and thus their main goal is to populate the `GDScriptParser::Datatype` of the underlying AST node. The datatype is then used to typecheck code that depends on this expression, and gives the compiler necessary information to generate appropriate, safe, and optimized bytecode.
For example, function calls are handled with `reduce_call()`, which must figure out what function is being called and check that the passed arguments match the function's parameters. The type of the underlying `CallNode` will be the return type of the function.
Another example is `reduce_identifier()`, which does _a lot_ of work: given the string of its `IdentifierNode`, it must figure out what that identifier refers to. It could be a local variable, class name, global or class function, function parameter, class or superclass member, or any number of other things. It has to check many different places to find this information!
A secondary goal of the `reduce` functions is to perform [constant folding](https://en.wikipedia.org/wiki/Constant_folding): to determine whether an expression is constant, and if it is, compute its _reduced value_ at this time so it does not need to be computed over and over at runtime!
- The resolve functions work on AST nodes that represent statements, and don't necessarily have values. Their goal is to do work related to program control flow, resolve their child AST nodes, deal with scoping, etc. One of the simplest examples is `resolve_if()`, which reduces the `if` condition, then resolves the `if` body and `else` body if it exists.
The `resolve_for()` function does more work than simply resolving its code block. With `for i in range(10)`, for example, it must also declare and type the new variable `i` within the scope of its code block, as well as make sure `range(10)` is iterable, among other things.
To understand classes and inheritance without introducing cyclic dependency problems that would come from immediate full class code analysis, the analyzer often asks only for class _interfaces_: it needs to know what member variables and methods exist as well as their types, but no more.
This is done through `resolve_class_interface()`, which populates `ClassNode`'s `Datatype` with that information. It first checks for superclass information with `resolve_class_inheritance()`, then populates its member information by calling `resolve_class_member()` on each member. Since this step is only about the class _interface_, methods are resolved with `resolve_function_signature()`, which gets all relevant typing information without resolving the function body!
The remaining steps of resolution, including member variable initialization code, method code, etc, can happen at a later time.

In fully untyped code, very little static analysis is possible. For example, the analyzer cannot know whether `my_var.some_member` exists when it does not know the type of `my_var`. Therefore, it cannot emit a warning or error because `some_member` _could_ exist - or it could not. The analyzer must trust the programmer. If an error does occur, it will be at runtime.
However, GDScript is gradually typed, so all of these analyses must work when parts of the code are typed and others untyped. Static analysis in a gradually typed language is a best-effort situation: suppose there is a typed variable `var x : int`, and an untyped `var y = "some string"`. We can obviously tell this isn't going to work, but the analyzer will accept the assignment `x = y` without warnings or errors: it only knows that `y` is untyped and can therefore be anything, including the `int` that `x` expects. It must once again trust the programmer to have written code that works. In this instance, the code will error at runtime.
In both these cases, the analyzer handles the uncertainty of untyped code by calling `mark_node_unsafe()` on the respective AST node. This means it didn't have enough information to know whether the code was fully safe or necessarily wrong. Lines with unsafe AST nodes are represented by gray line numbers in the GDScript editor. Green line numbers indicate a line of code without any unsafe nodes.

This analysis step is also where dependencies are introduced and that information stored for use later. If class `A` extends class `B` or contains a member with type `B` from some other script file, then the analyzer will attempt to load that second script. If `B` contains references to `A`, then a _cyclic_ dependency is introduced. This is OK in many cases, but impossible to resolve in others.

Clearly, the analyzer is where a lot of the "magic" happens! It determines what constitutes proper code that can actually be compiled, and provides as many safety guarantees as possible with the typing information it is provided with. The more typed the code, the safer and more optimized it will be!


#### Cyclic dependencies and member resolution

Cyclic dependencies from inheritance (`A extends B, B extends A`) are not supported in any programming language. Other cyclic dependencies are supported, such as `A extends B` and `B` uses, contains, or preloads, members of type `A`.

To see why cyclic dependencies are complicated, suppose there is one between classes `A <-> B`. Partially through the analysis of `A`, we will need information about `B`, and therefore trigger its analysis. However, the analysis of `B` will eventually need information from `A`, which is incomplete because we never finished analyzing it. This would result in members not being found when they actually exist!

GDScript supports cyclic dependencies due to a few features of the analyzer:

1. Class interface resolution: when analyzing code of class `A` that depends on some other class `B`, we don't need to resolve the _code_ of `B` (its member initializers, function code, etc). We only need to know what members and methods the class has, as well as their types. These are the only things one class can use to work with, or _interface_ with, another. Because of inheritance, a class's interface depends on its superclass as well, so recursive interface resolution is needed. More details can be found in `GDScriptAnalyzer::resolve_class_interface()`.
2. Out of order member resolution: the analyzer may not even need an entire class interface to be resolved in order to figure out a specific type! For example, if class `A` contains code that references `B.is_alive`, then the analyzer doesn't need to immediately resolve `B`'s entire interface. It may simply check whether `is_alive` exists in `B`, and reduce it for its type information, on-demand.
A fundamental cyclic dependency problem occurs when the types of two different member variables are mutually dependent. This is commonly checked by a pattern that declares a temporary datatype with `GDScriptParser::DataType resolving_datatype;`, followed by `resolving_datatype.kind = GDScriptParser::DataType::RESOLVING;`. If the analyzer attempts to resolve a member on-demand that is already tagged as resolving, then a cyclic dependency problem has been found and can be reported.


### Compiling (see [`GDScriptCompiler`](gdscript_compiler.h))

Compiling is the final step in making a GDScript executable in the [virtual machine](gdscript_vm.h) (VM). The compiler takes a `GDScript` object and an AST, and uses another class, [`GDScriptByteCodeGenerator`](gdscript_byte_codegen.h), to generate bytecode corresponding to the class. In doing this, it creates the objects that the VM understands how to run, like [`GDScriptFunction`](gdscript_function.h), and completes a few extra tasks needed for compilation, such as populating runtime class member information.

Importantly, the compilation process of a class, specifically the `GDScriptCompiler::_compile_class()` method, _cannot_ depend on information obtained by calling `GDScriptCompiler::_compile_class()` on another class, for the same cyclic dependency reasons explained in the previous section.
Any information that can only be obtained or populated during the compilation step, when `GDScript` objects become available, must be handled before `GDScriptCompiler::_compile_class()` is called. This process is centralized in `GDScriptCompiler::_prepare_compilation()` which works as the compile-time equivalent of `GDScriptAnalyzer::resolve_class_interface()`: it populates a `GDScript`'s "interface" exclusively with information from the analysis step, and without processing other external classes. This information may then be referenced by other classes without introducing problematic cycles.

The more typing information a GDScript has, the more optimized the compiled bytecode can be. For example, if `my_var` is untyped, the bytecode for `my_var.some_member` will need to go through several layers of indirection to figure out the type of `my_var` at runtime, and from there determine how to obtain `some_member`. This varies depending on whether `my_var` is a dictionary, a script, or a native class. If the type of `my_var` was known at compile time, the bytecode can directly call the type-specific method for obtaining a member.
Similar optimizations are possible for `my_var.some_func()`. With untyped GDScript, the VM will need to resolve `my_var`'s type at runtime, then, depending on the type, use different methods to resolve the function and call it. When the function is fully resolved during static analysis, native function pointers or GDScript function objects can be compiled into the bytecode and directly called by the VM, removing several layers of indirection.

Typed code is safer code and faster code!


## Loading scripts

GDScripts can be loaded in a couple of different ways. The main method, used almost everywhere in the engine, is to load scripts through the `ResourceLoader` singleton. In this way, GDScripts are resources like any others: `ResourceLoader::load()` will simply reroute to `ResourceFormatLoaderGDScript::load()`, found in `gdscript.h/cpp`(gdscript.h). This generates a GDScript object which is compiled and ready to use.

The other method is to manually load the source code, then pass it to a parser, then to an analyzer and then to a compiler. The previous approach does this behind the scenes, alongside some smart caching of scripts and other functionalities. It is used in the [GDScript test runner infrastructure](tests/gdscript_test_runner.h).


### Full and shallow scripts

The `ResourceFormatLoaderGDScript::load()` method simply calls `GDScriptCache::get_full_script()`. The [`GDScriptCache`](gdscript_cache.h) is, as it sounds, a cache for GDScripts. Its two main methods, `get_shallow_script()` and `get_full_script()`, get and cache, respectively, scripts that have been merely parsed, and scripts which have been statically analyzed and fully compiled. Another internal class, `GDScriptParserRef`, found in the same file, provides even more granularity over the different steps of the parsing process, and is used extensively in the analyzer.

Shallow, or "just parsed" scripts, provide information such as defined classes, class members, and so forth. This is sufficient for many purposes, like obtaining a class interface or checking whether a member exists on a specific class. Full scripts, on the other hand, have been analyzed and compiled and are ready to use.

The distinction between full and shallow scripts is very important, as shallow scripts cannot create cyclic dependency problems, whereas full scripts can. The analyzer, for example, never asks for full scripts. Choosing when to request a shallow vs a full script is an important but subtle decision.

In practice, full scripts are simply scripts where `GDScript::reload()` has been called. This critical function is the primary way in which scripts get compiled in Godot, and essentially does all the compilation steps covered so far in order. Whenever a script is loaded, or updated and reloaded in Godot, it will end up going through `GDScript::reload()`, except in very rare circumstances like the test runner. It is an excellent place to start reading and understanding the GDScript module!


## Special types of scripts

Certain types of GDScripts behave slightly differently. For example, autoloads are loaded with `ResourceLoader::load()` during `Main::start()`, very soon after Godot is launched. Many systems aren't initialized at that time, so error reporting is often significantly reduced and may not even show up in the editor.

Tool scripts, declared with the `@tool` annotation on a GDScript file, run in the editor itself as opposed to just when the game is launched. This leads to a significant increase in complexity, as many things that can be changed in the editor may affect a currently executing tool script.


## Other

There are many other classes in the GDScript module. Here is a brief overview of some of them:

- Declaration of GDScript warnings in [`GDScriptWarning`](gdscript_warning.h).
- [`GDScriptFunction`](gdscript_function.h), which represents an executable GDScript function. The relevant file contains both static as well as runtime information.
- The [virtual machine](gdscript_vm.cpp) is essentially defined as calling `GDScriptFunction::call()`.
- Editor-related functions can be found in parts of `GDScriptLanguage`, originally declared in [`gdscript.h`](gdscript.h) but defined in [`gdscript_editor.cpp`](gdscript_editor.cpp). Code highlighting can be found in [`GDScriptSyntaxHighlighter`](editor/gdscript_highlighter.h).
- GDScript decompilation is found in [`gdscript_disassembler.cpp`](gdscript_disassembler.h), defined as `GDScriptFunction::disassemble()`.
- Documentation generation from GDScript comments in [`GDScriptDocGen`](editor/gdscript_docgen.h)
