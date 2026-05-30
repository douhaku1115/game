CC      = gcc
CFLAGS  = -Wall -O2 -std=c11 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm
SRCS    = src/main.c src/game.c src/render.c src/sound.c
TARGET  = heian.exe

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
