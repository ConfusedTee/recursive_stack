CC=gcc
CFLAGS=-Wall -Wextra -Wno-implicit-fallthrough -std=gnu2x -fPIC -O2
LDFLAGS= -shared -shared -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=reallocarray -Wl,--wrap=free -Wl,--wrap=strdup -Wl,--wrap=strndup
OBJECTS = rstack.o 

librstack.so: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@
	
$(OBJECTS): %.o: %.c
	$(CC) -c $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	rm -f $(OBJECTS) librstack.so
