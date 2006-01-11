#!/bin/sh

# generate the list of enscript formatting options
LANGS=enscriptlangs.py
echo -n 'enscript_langs = [' > "$LANGS"
for i in `enscript --help-highlight | grep Name  | awk {'print $2'}`; do 
	echo -n "'$i', " >> "$LANGS"
done; echo ']' >> "$LANGS"

# generate the help file data
AUTHORS=authors.py
echo -n "authors='''" > "$AUTHORS"
cat AUTHORS >> "$AUTHORS"
echo "'''" >> "$AUTHORS"


