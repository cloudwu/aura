CFLAGS=-O2 -Wall
all : aura.exe
test : parser.exe words.exe stack.exe

aura.exe : aura.c astack.c aparser.c aword.c
	gcc $(CFLAGS) -o $@ $^ -DAURA_TESTMAIN

parser.exe : aparser.c
	gcc $(CFLAGS) -o $@ $^ -DPARSER_TESTMAIN

words.exe : aword.c
	gcc $(CFLAGS) -o $@ $^ -DWORD_TESTMAIN

stack.exe : astack.c
	gcc $(CFLAGS) -o $@ $^ -DSTACK_TESTMAIN

clean :
	rm -f *.exe
