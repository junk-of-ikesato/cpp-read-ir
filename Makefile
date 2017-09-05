
OBJS=main.o read-ir.o
TARGET=a.out

all: $(TARGET)

$(TARGET): $(OBJS)
	gcc -o $@ $+

%.o: %.c
	gcc -W -Wall -Wextra -Wconversion -Wno-unused-parameter -D__PUREC__ -c -o $@ $<

clean:
	rm -f *.o $(TARGET)