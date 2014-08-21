c2sqlite
========
c2sqlite uses libclang to put basic information of your c-files into a sqlite database. Current information that is
stored:
- which function calls which function
- declaration of functions
- function parameters

Build
-----
`make`

Usage
-----
`./c2sqlite c2sqlite.c`  
Now you find a file called `test.db`;  
you can open it with `sqlite3 test.db`.
