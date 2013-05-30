CC := $(CROSS_COMPILE)g++
LD := $(CROSS_COMPILE)ld

TARGET := bmp2splash
OBJS   := bmp2splash.o

CFLAGS = -O0 -Wall

all: $(TARGET)

clean: 
	rm -f *.o $(TARGET) *~

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bmp2splash.o: bmp2splash.cpp
