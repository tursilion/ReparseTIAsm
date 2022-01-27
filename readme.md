20210613

Just a quick and dirty tool used to merge several TI assembly files into one. I use this for my VGM compressor because people wanted something simpler than included 4-5 separate files and linking them together outside of gcc.

It enumerates refs, defs, equates, bss and more. It moves bss and equates to the top of the output file, strips refs and most of the tags (sections are history), and outputs defs at the top but commented out. equates are collapsed (if identical), and if a file has duplicate symbols to a file that came before it, the symbols are automatically renamed. (If those symbols were DEF'd, things will probably break, but that means you had duplicate DEF symbols anyway, so get to work!)

It tries to keep comments close to the lines that are moved, but the parser for all of the above is pretty primitive and let's face it, only I will ever need this. ;)
