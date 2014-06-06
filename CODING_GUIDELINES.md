# Coding Guidelines

The coding style adopted by Linux kernel source code is encouraged and should be used as much as possible. It can be found at: https://www.kernel.org/doc/Documentation/CodingStyle

Generally speaking, good codes speak for themselves instead of using verbose, distracting long variable names.

The one deviation from the Linux kernel coding style is the size of the tab key. As consistent with the value used in the original CoT package the size of the table key is set to four instead of eight spaces.

The following content can be put in the ~/.vimrc to this end:

	set ts=4
	set incsearch
	set hlsearch
	set showmatch
	let c_space_errors=1
	syntax enable
	syntax on

In particular, enabling "c_space_errors" option helps to highlight any white space errors.
