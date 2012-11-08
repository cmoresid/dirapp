CC		 = gcc
SOURCES  = mempool.c common.c client.c server.c dirapp.c 
OBJECTS  = $(SOURCES:.c=.o)
TARGET   = dirapp 
CFLAGS   = -g -c -Wall -Wno-sign-compare -Wno-pointer-sign
LDFLAGS	 = -lpthread

all: $(SOURCES) $(TARGET)

$(TARGET): $(OBJECTS) 
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm *.o
	-rm ${TARGET}

