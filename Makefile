all:	match textstats annofilter anno

match:	match.c
	gcc -std=c99 -Wall -Werror -o match match.c

textstats:	textstats.c
	gcc -std=c99 -Wall -Werror -o textstats textstats.c

annofilter:	annofilter.c
	gcc -std=c99 -Wall -Werror -o annofilter annofilter.c

anno:	anno.c
	gcc -std=c99 -Wall -Werror -o anno anno.c
