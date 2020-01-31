all:	match textstats

match:	match.c
	gcc -std=c99 -Wall -Werror -o match match.c

textstats:	textstats.c
	gcc -std=c99 -Wall -Werror -o textstats textstats.c
