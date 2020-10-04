### Styleguide

* Brace style: [K&R style](https://en.wikipedia.org/wiki/Indentation_style#K&R_style) with mandatory braces.
* Indent style: [Tabs for indentation, spaces for alignment](https://dmitryfrank.com/articles/indent_with_tabs_align_with_spaces).
* Blank lines should be completely empty (no indents).
* Class names and methods are `CamelCase`, plain functions are `snake_case`.
* Variable names are `camelCase`.
* Space after `if`, `while`, `for`, etc.
* Keep lines to around 80 columns. Not strict.
* Functions calls or declarations with lots of parameters should wrap onto
  multiple lines, one parameter per line.
* Source files are `snake_case.{cpp,hpp}`.

Example:
```
// In my_file.cpp
int my_plain_func(
	MyObject &argumentOne,
	int argumentTwo,
	int argumentThree,
	uint32_t argumentFour)
{
	if (argumentTwo > argumentThree) {
		argumentFour += 42;
	}
	...
	return ...;
}
```
