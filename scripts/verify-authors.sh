#!/bin/sh
# Run before publish: ensure all commits show one expected author.
authors=$(git log --format='%an' 2>/dev/null | sort -u)
[ -z "$authors" ] && exit 0
count=$(echo "$authors" | wc -l)
[ "$count" -eq 1 ] && [ "$authors" = "Jared Mahotiere" ] && exit 0
echo "Unexpected author(s):" >&2
echo "$authors" >&2
exit 1
