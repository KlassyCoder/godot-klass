func test():
	# Mid-line "#@if" is not at the start of the line, so it is an ordinary
	# trailing comment, not a directive (D2a). No error, no directive nesting.
	var x = 1 #@if test_a
	print(x)
