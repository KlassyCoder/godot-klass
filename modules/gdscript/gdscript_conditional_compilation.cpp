/**************************************************************************/
/*  gdscript_conditional_compilation.cpp                                  */
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

#include "gdscript_conditional_compilation.h"

#include "core/config/project_settings.h"
#include "core/os/os.h"
#include "core/string/char_utils.h"

// Maximum parenthesis nesting depth accepted by the condition-expression parser. Bounds
// recursion for pathological input; exceeding it is a hard error (see D4 in the design).
static constexpr int CONDITION_MAX_DEPTH = 32;

Mutex GDScriptConditionalCompilation::mutex;
GDScriptConditionalCompilation::FlagSet GDScriptConditionalCompilation::cached_settings_flags;
bool GDScriptConditionalCompilation::cache_valid = false;
bool GDScriptConditionalCompilation::has_override = false;
GDScriptConditionalCompilation::FlagSet GDScriptConditionalCompilation::override_flags;

bool GDScriptConditionalCompilation::FlagSet::has(const StringName &p_flag) const {
	const String lower = String(p_flag).to_lower();
	if (flags.has(StringName(lower))) {
		return true;
	}
	if (consult_os_features) {
		OS *os = OS::get_singleton();
		ERR_FAIL_NULL_V(os, false);
		return os->has_feature(lower);
	}
	return false;
}

void GDScriptConditionalCompilation::set_override_flags(const FlagSet &p_flags) {
	MutexLock lock(mutex);
	override_flags = p_flags;
	has_override = true;
}

void GDScriptConditionalCompilation::clear_override_flags() {
	MutexLock lock(mutex);
	has_override = false;
	override_flags = FlagSet();
}

GDScriptConditionalCompilation::FlagSet GDScriptConditionalCompilation::get_active_flags() {
	MutexLock lock(mutex);
	if (has_override) {
		return override_flags;
	}
	if (!cache_valid) {
		FlagSet fs;
		fs.consult_os_features = true;
		const PackedStringArray raw = GLOBAL_GET("gdscript/conditional_compilation/flags");
		for (const String &entry : raw) {
			const String stripped = entry.strip_edges().to_lower();
			if (!stripped.is_empty()) {
				fs.flags.insert(StringName(stripped));
			}
		}
		cached_settings_flags = fs;
		cache_valid = true;
	}
	return cached_settings_flags;
}

void GDScriptConditionalCompilation::invalidate_settings_cache() {
	MutexLock lock(mutex);
	cache_valid = false;
}

void GDScriptConditionalCompilation::cleanup() {
	MutexLock lock(mutex);
	cached_settings_flags = FlagSet();
	override_flags = FlagSet();
	cache_valid = false;
	has_override = false;
}

GDScriptConditionalCompilation::Directive GDScriptConditionalCompilation::identify_directive(const String &p_line, int p_line_offset, int &r_body_start) {
	const int len = p_line.length();
	int pos = p_line_offset;

	// (a) The first non-whitespace characters on the line must be '#' immediately
	// followed by '@' (no space between them).
	while (pos < len && (p_line[pos] == ' ' || p_line[pos] == '\t')) {
		pos++;
	}
	if (pos >= len || p_line[pos] != '#') {
		return DIRECTIVE_NONE;
	}
	pos++; // Past '#'.
	if (pos >= len || p_line[pos] != '@') {
		return DIRECTIVE_NONE;
	}
	pos++; // Past '@'.

	// (b) The maximal run of ASCII letters right after '#@' is the keyword. An empty run
	// (e.g. "#@", "#@ if", "#@(") is a hard error, not a fallback to comment.
	const int keyword_start = pos;
	while (pos < len && is_ascii_alphabet_char(p_line[pos])) {
		pos++;
	}
	const String keyword = p_line.substr(keyword_start, pos - keyword_start);
	r_body_start = pos;

	if (keyword == "if") {
		return DIRECTIVE_IF;
	} else if (keyword == "elif") {
		return DIRECTIVE_ELIF;
	} else if (keyword == "else") {
		return DIRECTIVE_ELSE;
	} else if (keyword == "endif") {
		return DIRECTIVE_ENDIF;
	}
	return DIRECTIVE_UNKNOWN;
}

void GDScriptConditionalCompilation::ConditionParser::_skip_ws() {
	while (pos < text.length() && (text[pos] == ' ' || text[pos] == '\t')) {
		pos++;
	}
}

bool GDScriptConditionalCompilation::ConditionParser::_match_word(const char *p_word) {
	_skip_ws();
	int word_len = 0;
	while (p_word[word_len] != '\0') {
		word_len++;
	}
	if (pos + word_len > text.length()) {
		return false;
	}
	for (int i = 0; i < word_len; i++) {
		if (text[pos + i] != (char32_t)p_word[i]) {
			return false;
		}
	}
	// If the matched token ends in an identifier character (e.g. "and", "not"), make sure
	// it isn't actually a prefix of a longer identifier (e.g. "android").
	const char32_t last_char = (char32_t)p_word[word_len - 1];
	if (is_ascii_identifier_char(last_char)) {
		const char32_t next_char = (pos + word_len < text.length()) ? text[pos + word_len] : U'\0';
		if (is_ascii_identifier_char(next_char)) {
			return false;
		}
	}
	pos += word_len;
	return true;
}

bool GDScriptConditionalCompilation::ConditionParser::_parse_primary(bool &r_result, int p_depth) {
	_skip_ws();

	if (_match_word("true")) {
		r_result = true;
		return true;
	}
	if (_match_word("false")) {
		r_result = false;
		return true;
	}

	if (pos < text.length() && text[pos] == '(') {
		if (p_depth >= CONDITION_MAX_DEPTH) {
			error = vformat("Too many nested parentheses in conditional compilation expression (limit %d).", CONDITION_MAX_DEPTH);
			return false;
		}
		pos++; // Consume '('.
		if (!_parse_or(r_result, p_depth + 1)) {
			return false;
		}
		_skip_ws();
		if (pos >= text.length() || text[pos] != ')') {
			error = "Expected \")\" in conditional compilation expression.";
			return false;
		}
		pos++; // Consume ')'.
		return true;
	}

	const int start = pos;
	if (pos < text.length() && (is_ascii_alphabet_char(text[pos]) || text[pos] == '_')) {
		while (pos < text.length() && is_ascii_identifier_char(text[pos])) {
			pos++;
		}
		const String flag_name = text.substr(start, pos - start);
		// An undefined identifier evaluates to `false`, matching the C preprocessor and Swift.
		r_result = flags.has(StringName(flag_name));
		return true;
	}

	error = "Expected an identifier, \"true\", \"false\", or \"(\" in conditional compilation expression.";
	return false;
}

bool GDScriptConditionalCompilation::ConditionParser::_parse_unary(bool &r_result, int p_depth) {
	if (_match_word("!") || _match_word("not")) {
		// Chained negations recurse just like parentheses do, so they need the same cap; the
		// operand count is otherwise bounded only by the length of the source line.
		if (p_depth >= CONDITION_MAX_DEPTH) {
			error = vformat("Too many nested operators in conditional compilation expression (limit %d).", CONDITION_MAX_DEPTH);
			return false;
		}
		bool inner = false;
		if (!_parse_unary(inner, p_depth + 1)) {
			return false;
		}
		r_result = !inner;
		return true;
	}
	return _parse_primary(r_result, p_depth);
}

bool GDScriptConditionalCompilation::ConditionParser::_parse_and(bool &r_result, int p_depth) {
	bool left = false;
	if (!_parse_unary(left, p_depth)) {
		return false;
	}
	while (_match_word("&&") || _match_word("and")) {
		bool right = false;
		if (!_parse_unary(right, p_depth)) {
			return false;
		}
		left = left && right;
	}
	r_result = left;
	return true;
}

bool GDScriptConditionalCompilation::ConditionParser::_parse_or(bool &r_result, int p_depth) {
	bool left = false;
	if (!_parse_and(left, p_depth)) {
		return false;
	}
	while (_match_word("||") || _match_word("or")) {
		bool right = false;
		if (!_parse_and(right, p_depth)) {
			return false;
		}
		left = left || right;
	}
	r_result = left;
	return true;
}

Error GDScriptConditionalCompilation::evaluate_condition(const String &p_condition, const FlagSet &p_flags, bool &r_result, String &r_error) {
	ConditionParser parser{ p_condition, 0, p_flags, String() };

	bool result = false;
	if (!parser._parse_or(result, 0)) {
		r_error = parser.error.is_empty() ? "Invalid conditional compilation expression." : parser.error;
		return ERR_PARSE_ERROR;
	}

	parser._skip_ws();
	if (parser.pos != p_condition.length()) {
		r_error = vformat("Unexpected text %s in conditional compilation expression.", p_condition.substr(parser.pos).strip_edges());
		return ERR_PARSE_ERROR;
	}

	r_result = result;
	return OK;
}
