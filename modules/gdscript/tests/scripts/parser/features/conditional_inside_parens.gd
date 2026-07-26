func test():
	# Inside an open bracket, a line starting with "#@if" is just a comment:
	# the tokenizer never enters directive handling while `paren_stack` is
	# non-empty, so this does not open a conditional block.
	var arr = [
		1,
		#@if test_a
		2,
	]
	print(arr)
