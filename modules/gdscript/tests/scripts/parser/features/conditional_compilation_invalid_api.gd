func test():
	#@if false
	# None of this must ever reach the parser: a call to a method that does not
	# exist anywhere, plus text that is not even valid GDScript syntax. If the
	# tokenizer ever tokenized this branch, parsing would fail.
	this_method_does_not_exist_on_any_object_ever()
	@#$%^&*() this is not valid gdscript at all !!! ===>>>
	class_name ThisWouldConflict extends SomeUndefinedBaseClass99
	#@else
	print("compiled correctly")
	#@endif
