" Syntax highlighting for monotone basic_io format 
" used by 'read-permissions', and 'write-permissions'
"
" Set up keywords and patterns we want to highlight
syn match basicIOSymbol /[a-z_]*/
syn region basicIOString start=+"+  skip=+\\\\\|\\"+  end=+"+
syn match basicIOHexID /\[[a-fA-F0-9]*\]/

" Set up sane default highlighting links, such as basicIOString->String
hi def link basicIOSymbol	Keyword
hi def link basicIOString	String
hi def link basicIOHexID	Identifier

