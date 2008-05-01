grammar selectors;

options {
  language = C;
}

input : sequence EOF;

sequence : addition ( ',' addition )*;

addition : intersection ( ('+'|'~') intersection )*;

intersection : antique ( ('^'|'&') antique )*;

antique : selector ( '/' selector )*; // 'antique' (c) Richard Levitte

selector :
  immediate
| '(' addition ')'
//| Difference
//| PrefixOperator
| function1 '(' addition ')'
//| Operator2 "(" Number10 "," Selector ")"
;

immediate :
  Identifier
| branchName
| authorName
;

function1 :
  'eca'
| 'lca'
| 'parents' | 'p'
| 'ancestors' | 'anc' | 'a'
| 'union' | 'difference' | 'intersection'
;

Identifier : 'i:' ('0'..'9'|'a'..'f'|'A'..'F')+;

branchName : 'b:' anyString;

authorName : 'a:' anyString;

anyString : StringLiteral | StringQuoted | Globish | PCRE;

StringLiteral : ('a'..'z'|'0'..'9'|'-'|'_'|'.'|'@')+;
StringQuoted : '"' (~('"'|'\\')|'\\' .)+ '"';
Globish : '<' (~('>'|'\\')|'\\' .)+ '>';
PCRE : '#' (~('#'|'\\')|'\\' .)+ '#';
