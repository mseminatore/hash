ARCH = $(shell uname -m)
TARGET = ht_test
OBJS = hash.o
DEPS = hash.h
CFLAGS += -g -O2 #-DNDEBUG
LIBNAME = libht.a
LFLAGS += -L. -lht #-lm

all: $(LIBNAME) ht_test
	
$(LIBNAME): $(OBJS)
	ar rcs $(LIBNAME) $(OBJS)

%.o: %.c $(DEPS)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

ht_test: $(LIBNAME) ./testy/test_main.o test.o
	$(CC) -o $@ $^ $(LFLAGS)

test: ht_test
	./ht_test

clean:
	rm $(TARGET) $(OBJS) $(LIBNAME) test.o

