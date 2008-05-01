grammar selectors;

//setOperator : ( 'heads' | 'h' | 'erase_ancestors' | 'parent' | 'p' | 'leaves' );

input : difference EOF;

difference : union ( '-' union )*;

union : intersection ( ',' intersection )*;

intersection : selector ( '/' selector )*;

selector :
  immediate
| '(' difference ')'
//| Difference
//| PrefixOperator
| function1 '(' difference ')'
//| Operator2 "(" Number10 "," Selector ")"
;

immediate :
  Identifier
| BranchName
| AuthorName
;

function1 :
  'h' | 'heads'
| 'lca'
| 'p' | 'parents'
;

Identifier : 'i:' ('0'..'9'|'a'..'f'|'A'..'F')+;

BranchName : 'b:' (
  ('a'..'z'|'0'..'9'|'-'|'_'|'.'|'@')+
| '"' (~('\r'|'\n'|'\t'|' '|'\''|'"'|'\\')|'\\' .)+ '"'
);

AuthorName : 'a:' (
  ('a'..'z'|'0'..'9'|'-'|'_'|'.'|'@')+
| '"' (~('\r'|'\n'|'\t'|' '|'\''|'"'|'\\')|'\\' .)+ '"'
);
