CC		 = gcc
SOURCES  = common.c client.c server.c dirapp.c 
OBJECTS  = $(SOURCES:.c=.o)
TARGET   = dirapp 
CFLAGS   = -g -c -Wall
LDFLAGS	 = -lpthread

all: $(SOURCES) $(TARGET)

$(TARGET): $(OBJECTS) 
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm *.o
	-rm ${TARGET}
	
