execvar: main.c
	gcc main.c -o execvar -ldl -Wall -Wextra

.phony: clean

clean:
	rm execvar
