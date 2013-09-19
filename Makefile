all: clean
	gcc -std=c99 -Wall pnstat.c -o pnstat

clean:
	rm -f pnstat
