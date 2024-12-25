ifneq (,$(wildcard .env))
	include .env
endif

build:
	gcc main.c -o bin/main -framework VideoToolbox -framework CoreMedia -framework CoreFoundation -framework CoreVideo -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -D_THREAD_SAFE -L/opt/homebrew/lib -lSDL2

run:
	./bin/main