# Coding Guidelines

The coding style of ONEDC oBIX applications use the GNU / Linux kernel source code guidelines, featured at https://www.kernel.org/doc/Documentation/CodingStyle, with a few exceptions as noted below. 

# Exceptions

A list of exceptions to the GNU / Linux kernel coding guidelines are listed below.

## Tabular Key

Generally, C-style code files (E.g. code contained within files suffixed with a `.c` or `.h` extension) must be indented with a tab key instead of multiple space characters. The tab key must be set to a size-width of **four (4) characters** for the existing source-code to line up properly.

*Note*: Please do not change the indenting of other people's code if the tab-character in your editor is not set to four (4) characters. 

## Use of `typedef`

The GNU / Linux coding guidelines discourage the type defining of structures and enumerations. This rule is not followed in ONEDC / oBIX, however typedefines **must** be suffixed with a `_t`:

```c

typedef struct data_structure {
	...
} data_structure_t;

```

## Braces

**Braces in conditional statements must be used at all times.**  Leaving out braces in conditionals is considered bad practice and leaves code open for undefined behaviour in quick edits; it's simply too easy to invoke undefined behaviour modifying code sans braces.

[CVE-2014-1266](http://web.nvd.nist.gov/view/vuln/detail?vulnId=CVE-2014-1266) may have been avoided by the use of braces.

The following example is discouraged:

```c
	if (conditional)
		do something;
	else
		do something else;
```

Please use this instead:

```c
	if (conditional) {
		do something;
	} else {
		do something else;
	}
```

## Comparisons

Comparisons in C can be tricky. As such, always compare the result of a function with its return type in conditionals and branches. The use of terse expressions in conditionals is encouraged, but only if it does not complicate readability of the expression.

* Please always compare pointer types with NULL, another pointer type, or use the logical NOT operator `!`

```c
char *buffer;
if ((buffer = (char *)malloc(64)) == NULL) {

}
```

* Please avoid using BOOL macros or structures to compare with integers.
* Please write functions that are not fire-and-forget return an `int` with `0` meaning success, and `<0` on failure.
* Please refrain from comparing NULL with the `int`eger 0, and vice-versa.
* Please refrain from using the logical NOT `!` operator to compare integer types:

```c
if (!strcmp(string, "antoher string")) {
	this_is_bad();
}

if (strcmp(string, "another string") == 0) {
	please_use_this_method();
}
```

## Vi iMproved Example

The following content can be put in the `~/.vimrc` file to help you code for ONEDC / oBIX:

```bash
	set ts=4
	set incsearch
	set hlsearch
	set showmatch
	let c_space_errors=1
	syntax enable
	syntax on
```

In particular, enabling the `c_space_errors` option helps to highlight any white space errors.
