c2sqlite: c2sqlite.c
	clang c2sqlite.c `llvm-config --cflags --libs` -lclang -o c2sqlite -lsqlite3 -Wall -pedantic -ggdb

.PHONY: clean
clean:
	rm -fv c2sqlite test.db core.*
