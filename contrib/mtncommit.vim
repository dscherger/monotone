" Syntax highlighting for monotone commit log messages
"
" Set up keywords and patterns we want to highlight
syn match mtnCommitAdded /\ \ added/
syn match mtnCommitCancelLine /\*\*\* REMOVE THIS LINE TO CANCEL THE COMMIT \*\*\*/
" Highlight cert names such as Author: and Branch:
syn match mtnCommitCertName /^.*: /
syn match mtnCommitComment /--.*$/
syn match mtnCommitPatched /\ \ patched/
syn match mtnCommitRemoved /\ \ removed/

" Set up sane default highlighting links, such as mtnCommitComment->Comment
hi def link mtnCommitAdded	diffAdded
hi def link mtnCommitCancelLine	Error
hi def link mtnCommitCertName	Identifier
hi def link mtnCommitComment	Comment
hi def link mtnCommitPatched	diffChanged
hi def link mtnCommitRemoved	diffRemoved


