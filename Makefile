procnanny_server: procnanny_server.c
	gcc -Wall -DMEMWATCH -DMW_STDIO procnanny_server.c memwatch.c -o procnanny.server
	gcc -Wall -DMEMWATCH -DMW_STDIO procnanny_client.c memwatch.c -o procnanny.client

.PHONY: clean
clean:
	-rm -f procnanny *.o core
