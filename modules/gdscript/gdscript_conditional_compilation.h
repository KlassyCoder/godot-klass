/**************************************************************************/
/*  gdscript_conditional_compilation.h                                    */
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

#include "core/error/error_list.h"
#include "core/os/mutex.h"
#include "core/string/string_name.h"
#include "core/templates/hash_set.h"

// Evaluates `#@if`/`#@elif`/`#@else`/`#@endif` conditional compilation directives for
// GDScript. The directive-recognition and condition-evaluation logic lives here so it
// can be unit-tested in isolation; the tokenizer (`GDScriptTokenizerText`) is the only
// place that actually resolves directives while scanning source code.
class GDScriptConditionalCompilation {
public:
	enum Directive {
		DIRECTIVE_NONE, // Line does not start with '#@'.
		DIRECTIVE_UNKNOWN, // '#@' present but keyword unrecognized (or empty).
		DIRECTIVE_IF,
		DIRECTIVE_ELIF,
		DIRECTIVE_ELSE,
		DIRECTIVE_ENDIF,
	};

	struct FlagSet {
		HashSet<StringName> flags; // Always stored lowercased.
		bool consult_os_features = true; // false => closed world (export, tests).
		bool has(const StringName &p_flag) const;
	};

	static void set_override_flags(const FlagSet &p_flags);
	static void clear_override_flags();
	static FlagSet get_active_flags(); // Returns by value; safe to copy into a tokenizer on any thread.
	static void invalidate_settings_cache();

	// Releases the `StringName`s held by the static flag sets. Must run during module teardown:
	// `StringName::cleanup()` happens before static destructors, so anything still held here would
	// unref after the name pool is gone and trip `ERR_FAIL_COND(!configured)`.
	static void cleanup();

	// Implements the D2 recognition + validation rule on a single line's text starting at
	// `p_line_offset` (which points at the '#'). Returns DIRECTIVE_NONE if there is no '#@',
	// DIRECTIVE_UNKNOWN if '#@' is present with a bad keyword, and otherwise sets
	// `r_body_start` to the index just past the keyword.
	static Directive identify_directive(const String &p_line, int p_line_offset, int &r_body_start);

	static Error evaluate_condition(const String &p_condition, const FlagSet &p_flags, bool &r_result, String &r_error);

private:
	static Mutex mutex;
	static FlagSet cached_settings_flags;
	static bool cache_valid;
	static bool has_override;
	static FlagSet override_flags;

	struct ConditionParser {
		const String &text;
		int pos = 0;
		const FlagSet &flags;
		String error;

		void _skip_ws();
		bool _match_word(const char *p_word);
		bool _parse_or(bool &r_result, int p_depth);
		bool _parse_and(bool &r_result, int p_depth);
		bool _parse_unary(bool &r_result, int p_depth);
		bool _parse_primary(bool &r_result, int p_depth);
	};
};
