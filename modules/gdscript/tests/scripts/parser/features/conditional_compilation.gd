func test():
	#@if test_a
	print("if test_a")
	#@endif

	#@if !test_a
	print("wrong: not test_a")
	#@else
	print("else branch")
	#@endif

	#@if test_a && test_b
	print("and true")
	#@endif

	#@if test_a and test_b
	print("and true word")
	#@endif

	#@if test_a || undefined_flag
	print("or true")
	#@endif

	#@if test_a or undefined_flag
	print("or true word")
	#@endif

	#@if not test_a
	print("wrong: not test_a elif")
	#@elif test_b
	print("elif test_b")
	#@endif

	#@if (test_a) && (test_b)
	print("parens true")
	#@endif

	#@if !(test_a && false)
	print("precedence with not")
	#@endif

	# '&&' binds tighter than '||', so this is `false || (test_a && test_b)`.
	#@if false || test_a && test_b
	print("and binds tighter than or")
	#@endif

	#@if undefined_flag
	print("wrong: undefined_flag")
	#@elif undefined_flag_2
	print("wrong: undefined_flag_2")
	#@else
	print("final else")
	#@endif

	#@if test_a
	#@if test_b
	print("nested both true")
	#@else
	print("wrong: nested else")
	#@endif
	#@endif

	#@if true
	print("literal true")
	#@endif

	#@if false
	print("wrong: literal false")
	#@endif # trailing comment after a directive body is allowed
