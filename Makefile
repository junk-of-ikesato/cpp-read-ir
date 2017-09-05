
OBJS=main.o read-ir.o
TARGET=a.out

all: $(TARGET)

$(TARGET): $(OBJS)
	gcc -g -o $@ $+

%.o: %.c
	gcc -g -W -Wall -Wextra -Wconversion -Wno-unused-parameter -D__PUREC__ -c -o $@ $<

clean:
	rm -f *.o $(TARGET)