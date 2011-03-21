debugsrv: debugsrv.c
	gcc -std=c99 -o debugsrv -ggdb -Wall -lavahi-client debugsrv.c

clean:
	rm debugsrv

.PHONY: clean
