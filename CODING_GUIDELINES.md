# Coding Guidelines

The coding style for the ONEDC oBIX applications use the GNU/Linux kernel source code guidelines, featured at https://www.kernel.org/doc/Documentation/CodingStyle, with a few exceptions as noted below. 

# Exceptions

A list of exceptions to the GNU/Linux kernel coding guidelines are listed below.

## Tabular key

Generally, C-style code files (that is, code contained within files suffixed with a `.c` or `.h` extension) must be indented with a tab key instead of multiple space characters.  The tab key must be set to a size-width of **4 characters** for existing source-code to line up properly.

*Note*: Please do not change the indenting of other people''s code if the tab-character in your editor is not set to 4 characters. 

## USe of `typedef`

The GNU/Linux coding guidelines discourage the type defining of structures and enumerations.  This rule is not followed in ONEDC/oBIX, however typedefines must be suffixed with a `_t`:

```c

typedef struct data_structure {
	...
} data_structure_t;

```

## Braces

**Braces in conditional statements must be used at all times.**  Leaving out braces in conditionals is considered bad practise and leaves code open for undefined behaviour in quick edits; it''s simply too easy to invoke undefined behaviour modifying code sans braces.

[https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2014-0160](CVE-2014-0160) may have been avoided by the use of braces.

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

## Vi iMproved example

The following content can be put in the ~/.vimrc to this end:

```bash
	set ts=4
	set incsearch
	set hlsearch
	set showmatch
	let c_space_errors=1
	syntax enable
	syntax on
```

In particular, enabling "c_space_errors" option helps to highlight any white space errors.
