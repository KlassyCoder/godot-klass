/**************************************************************************/
/*  test_gdscript_conditional_compilation.h                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "../gdscript_conditional_compilation.h"
#include "../gdscript_tokenizer.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

TEST_CASE("[Modules][GDScript][ConditionalCompilation] identify_directive() truth table") {
	using GDCC = GDScriptConditionalCompilation;
	int body_start = 0;

	// No '@': never a directive, regardless of what follows '#'.
	CHECK(GDCC::identify_directive("#ifdef", 0, body_start) == GDCC::DIRECTIVE_NONE);
	CHECK(GDCC::identify_directive("#iframe", 0, body_start) == GDCC::DIRECTIVE_NONE);
	CHECK(GDCC::identify_directive("#region", 0, body_start) == GDCC::DIRECTIVE_NONE);
	CHECK(GDCC::identify_directive("# if", 0, body_start) == GDCC::DIRECTIVE_NONE);
	// Space between '#' and '@' breaks the "immediately followed" rule.
	CHECK(GDCC::identify_directive("# @if", 0, body_start) == GDCC::DIRECTIVE_NONE);

	// Valid keywords.
	CHECK(GDCC::identify_directive("#@if DEBUG", 0, body_start) == GDCC::DIRECTIVE_IF);
	CHECK(GDCC::identify_directive("#@elif DEBUG", 0, body_start) == GDCC::DIRECTIVE_ELIF);
	CHECK(GDCC::identify_directive("#@else", 0, body_start) == GDCC::DIRECTIVE_ELSE);
	CHECK(GDCC::identify_directive("#@endif", 0, body_start) == GDCC::DIRECTIVE_ENDIF);

	// Bad/empty keyword runs are hard errors, never a silent fallback to comment.
	CHECK(GDCC::identify_directive("#@ifdef", 0, body_start) == GDCC::DIRECTIVE_UNKNOWN);
	CHECK(GDCC::identify_directive("#@", 0, body_start) == GDCC::DIRECTIVE_UNKNOWN);
	CHECK(GDCC::identify_directive("#@ if", 0, body_start) == GDCC::DIRECTIVE_UNKNOWN);

	// No whitespace required between the keyword and a following '(' or '!'.
	CHECK(GDCC::identify_directive("#@if(DEBUG)", 0, body_start) == GDCC::DIRECTIVE_IF);
	CHECK(GDCC::identify_directive("#@if!DEBUG", 0, body_start) == GDCC::DIRECTIVE_IF);
}

TEST_CASE("[Modules][GDScript][ConditionalCompilation] evaluate_condition()") {
	using GDCC = GDScriptConditionalCompilation;

	GDCC::FlagSet flags;
	flags.consult_os_features = false;
	flags.flags.insert("debug");

	bool result = false;
	String error;

	SUBCASE("Bare flag, true and false") {
		CHECK(GDCC::evaluate_condition("DEBUG", flags, result, error) == OK);
		CHECK(result);
		CHECK(GDCC::evaluate_condition("MOBILE", flags, result, error) == OK);
		CHECK_FALSE(result); // Undefined identifiers evaluate to false.
	}

	SUBCASE("Case-insensitive flag matching") {
		CHECK(GDCC::evaluate_condition("debug", flags, result, error) == OK);
		CHECK(result);
		CHECK(GDCC::evaluate_condition("DeBuG", flags, result, error) == OK);
		CHECK(result);
	}

	SUBCASE("true and false literals") {
		CHECK(GDCC::evaluate_condition("true", flags, result, error) == OK);
		CHECK(result);
		CHECK(GDCC::evaluate_condition("false", flags, result, error) == OK);
		CHECK_FALSE(result);
	}

	SUBCASE("Negation, symbolic and word form") {
		CHECK(GDCC::evaluate_condition("!DEBUG", flags, result, error) == OK);
		CHECK_FALSE(result);
		CHECK(GDCC::evaluate_condition("not DEBUG", flags, result, error) == OK);
		CHECK_FALSE(result);
		CHECK(GDCC::evaluate_condition("!MOBILE", flags, result, error) == OK);
		CHECK(result);
	}

	SUBCASE("Conjunction and disjunction, symbolic and word form") {
		CHECK(GDCC::evaluate_condition("DEBUG && true", flags, result, error) == OK);
		CHECK(result);
		CHECK(GDCC::evaluate_condition("DEBUG and false", flags, result, error) == OK);
		CHECK_FALSE(result);
		CHECK(GDCC::evaluate_condition("MOBILE || DEBUG", flags, result, error) == OK);
		CHECK(result);
		CHECK(GDCC::evaluate_condition("MOBILE or false", flags, result, error) == OK);
		CHECK_FALSE(result);
	}

	SUBCASE("Precedence: '!' > '&&' > '||'") {
		// MOBILE(false) || DEBUG(true) && false == MOBILE || (DEBUG && false) == false || false == false.
		CHECK(GDCC::evaluate_condition("MOBILE || DEBUG && false", flags, result, error) == OK);
		CHECK_FALSE(result);
	}

	SUBCASE("Parentheses override precedence") {
		// (MOBILE || DEBUG) && false == true && false == false.
		CHECK(GDCC::evaluate_condition("(MOBILE || DEBUG) && false", flags, result, error) == OK);
		CHECK_FALSE(result);
		// (MOBILE || DEBUG) && true == true.
		CHECK(GDCC::evaluate_condition("(MOBILE || DEBUG) && true", flags, result, error) == OK);
		CHECK(result);
	}

	// Both recursive descents must be depth-capped. `!`/`not` chains used to recurse without a
	// cap, so a long enough run of them on one line could overflow the stack rather than
	// producing a clean parse error.
	SUBCASE("Deeply nested operators are a parse error, not a stack overflow") {
		String deep_not;
		for (int i = 0; i < 4096; i++) {
			deep_not += "!";
		}
		error = String();
		CHECK(GDCC::evaluate_condition(deep_not + "DEBUG", flags, result, error) != OK);
		CHECK_FALSE(error.is_empty());

		String deep_word_not;
		for (int i = 0; i < 4096; i++) {
			deep_word_not += "not ";
		}
		error = String();
		CHECK(GDCC::evaluate_condition(deep_word_not + "DEBUG", flags, result, error) != OK);
		CHECK_FALSE(error.is_empty());

		String deep_parens;
		for (int i = 0; i < 4096; i++) {
			deep_parens += "(";
		}
		error = String();
		CHECK(GDCC::evaluate_condition(deep_parens + "DEBUG", flags, result, error) != OK);
		CHECK_FALSE(error.is_empty());

		// Just under the cap must still evaluate normally.
		error = String();
		String ok_not;
		for (int i = 0; i < 30; i++) {
			ok_not += "!";
		}
		CHECK(GDCC::evaluate_condition(ok_not + "DEBUG", flags, result, error) == OK);
	}

	SUBCASE("Malformed expressions are parse errors, not silent falses") {
		CHECK(GDCC::evaluate_condition("", flags, result, error) != OK);
		CHECK_FALSE(error.is_empty());

		error = String();
		CHECK(GDCC::evaluate_condition("DEBUG &&", flags, result, error) != OK);
		CHECK_FALSE(error.is_empty());

		error = String();
		CHECK(GDCC::evaluate_condition("(DEBUG", flags, result, error) != OK);
		CHECK_FALSE(error.is_empty());

		error = String();
		CHECK(GDCC::evaluate_condition("DEBUG DEBUG", flags, result, error) != OK);
		CHECK_FALSE(error.is_empty());
	}
}

TEST_CASE("[Modules][GDScript][ConditionalCompilation] Excluded regions preserve line numbers") {
	const String code = String(
			"extends RefCounted\n"
			"#@if MOBILE\n"
			"func unused_a():\n"
			"	pass\n"
			"func unused_b():\n"
			"	pass\n"
			"#@endif\n"
			"func marker():\n"
			"	pass\n");

	GDScriptConditionalCompilation::FlagSet flags;
	flags.consult_os_features = false;

	GDScriptTokenizerText tokenizer;
	tokenizer.set_conditional_flags(flags);
	tokenizer.set_source_code(code);

	GDScriptTokenizer::Token token = tokenizer.scan();
	bool found_marker = false;
	while (token.type != GDScriptTokenizer::Token::TK_EOF) {
		if (token.type == GDScriptTokenizer::Token::IDENTIFIER && token.get_identifier() == StringName("marker")) {
			found_marker = true;
			// Line 8 in the source above (1-based), regardless of the 5 excluded lines before it.
			CHECK(token.start_line == 8);
			break;
		}
		token = tokenizer.scan();
	}
	CHECK(found_marker);
}

TEST_CASE("[Modules][GDScript][ConditionalCompilation] Multiline string content is never mistaken for a directive") {
	const String code = String(
			"extends RefCounted\n"
			"var s = \"\"\"\n"
			"#@endif\n"
			"\"\"\"\n"
			"func marker():\n"
			"	pass\n");

	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(code);

	bool found_error = false;
	bool found_marker = false;
	GDScriptTokenizer::Token token = tokenizer.scan();
	while (token.type != GDScriptTokenizer::Token::TK_EOF) {
		if (token.type == GDScriptTokenizer::Token::ERROR) {
			found_error = true;
		}
		if (token.type == GDScriptTokenizer::Token::IDENTIFIER && token.get_identifier() == StringName("marker")) {
			found_marker = true;
			CHECK(token.start_line == 5);
		}
		token = tokenizer.scan();
	}
	CHECK_FALSE(found_error);
	CHECK(found_marker);
}

// Regression test. `_advance()` re-enters `check_indent()` when it consumes the source's last
// character. A directive that sits on the final line is still mid-update at that moment, so the
// re-entrant EOF check used to report it as an unterminated `#@if` and clear `conditional_stack`,
// after which the directive's own handler saw an empty stack and reported a bogus second error.
TEST_CASE("[Modules][GDScript][ConditionalCompilation] Directive on the final line is not a false error") {
	auto scan_for_errors = [](const String &p_code) {
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code(p_code);
		bool found_error = false;
		GDScriptTokenizer::Token token = tokenizer.scan();
		while (token.type != GDScriptTokenizer::Token::TK_EOF) {
			found_error = found_error || token.type == GDScriptTokenizer::Token::ERROR;
			token = tokenizer.scan();
		}
		return found_error;
	};

	SUBCASE("#@endif as the last line, with trailing newline") {
		CHECK_FALSE(scan_for_errors("extends RefCounted\n#@if true\npass\n#@endif\n"));
	}

	SUBCASE("#@endif as the last line, without trailing newline") {
		CHECK_FALSE(scan_for_errors("extends RefCounted\n#@if true\npass\n#@endif"));
	}

	SUBCASE("#@endif as the last line, closing a skipped region") {
		CHECK_FALSE(scan_for_errors("extends RefCounted\n#@if false\npass\n#@endif\n"));
	}

	SUBCASE("#@endif as the last line, with a trailing comment") {
		CHECK_FALSE(scan_for_errors("extends RefCounted\n#@if true\npass\n#@endif # done\n"));
	}

	SUBCASE("Nested #@endif pair closing on the last line") {
		CHECK_FALSE(scan_for_errors("extends RefCounted\n#@if true\n#@if true\npass\n#@endif\n#@endif\n"));
	}

	// A genuinely unterminated `#@if` must still be reported once the guard stops suppressing it.
	SUBCASE("Unterminated #@if is still an error") {
		CHECK(scan_for_errors("extends RefCounted\n#@if true\npass\n"));
	}

	SUBCASE("Unterminated #@if on the very last line is still an error") {
		CHECK(scan_for_errors("extends RefCounted\n#@if true\n"));
	}

	// Reaching EOF inside a skipped region must not clear the stack out from under
	// `_skip_disabled_region()`, whose EOF handler dereferences `conditional_stack.back()`.
	SUBCASE("EOF inside a skipped region is an error, not a crash") {
		CHECK(scan_for_errors("extends RefCounted\n#@if false\npass\npass\n"));
	}
}

TEST_CASE("[Modules][GDScript][ConditionalCompilation] Structural errors") {
	SUBCASE("Unmatched #@endif") {
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code("extends RefCounted\n#@endif\n");
		bool found_error = false;
		GDScriptTokenizer::Token token = tokenizer.scan();
		while (token.type != GDScriptTokenizer::Token::TK_EOF) {
			found_error = found_error || token.type == GDScriptTokenizer::Token::ERROR;
			token = tokenizer.scan();
		}
		CHECK(found_error);
	}

	SUBCASE("#@elif after #@else") {
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code("extends RefCounted\n#@if true\npass\n#@else\npass\n#@elif true\npass\n#@endif\n");
		bool found_error = false;
		GDScriptTokenizer::Token token = tokenizer.scan();
		while (token.type != GDScriptTokenizer::Token::TK_EOF) {
			found_error = found_error || token.type == GDScriptTokenizer::Token::ERROR;
			token = tokenizer.scan();
		}
		CHECK(found_error);
	}

	SUBCASE("EOF while a #@if is still open") {
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code("extends RefCounted\n#@if true\npass\n");
		bool found_error = false;
		GDScriptTokenizer::Token token = tokenizer.scan();
		while (token.type != GDScriptTokenizer::Token::TK_EOF) {
			found_error = found_error || token.type == GDScriptTokenizer::Token::ERROR;
			token = tokenizer.scan();
		}
		CHECK(found_error);
	}
}

TEST_CASE("[Modules][GDScript][ConditionalCompilation] '#@if' inside parentheses is a plain comment") {
	const String code = String(
			"extends RefCounted\n"
			"var a = [\n"
			"	1, #@if DEBUG\n"
			"	2,\n"
			"]\n");

	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(code);

	bool found_error = false;
	GDScriptTokenizer::Token token = tokenizer.scan();
	while (token.type != GDScriptTokenizer::Token::TK_EOF) {
		found_error = found_error || token.type == GDScriptTokenizer::Token::ERROR;
		token = tokenizer.scan();
	}
	CHECK_FALSE(found_error);
}

TEST_CASE("[Modules][GDScript][ConditionalCompilation] Mid-line '#@if' is a comment, not a directive (D2a)") {
	const String code = "var x = 1 #@if FOO\n";

	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(code);

	bool found_error = false;
	GDScriptTokenizer::Token token = tokenizer.scan();
	while (token.type != GDScriptTokenizer::Token::TK_EOF) {
		found_error = found_error || token.type == GDScriptTokenizer::Token::ERROR;
		token = tokenizer.scan();
	}
	CHECK_FALSE(found_error);
}

} // namespace GDScriptTests
