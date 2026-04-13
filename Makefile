ARCH = $(shell uname -m)
TARGET = ht_test
OBJS = hash.o
CFLAGS += -g -O2 #-D_DEBUG #-DNDEBUG
LIBNAME = libht.a
LFLAGS += -L. -lht #-lm

all: $(LIBNAME) $(TARGET)
	
$(LIBNAME): $(OBJS)
	ar rcs $(LIBNAME) $(OBJS)

%.o: %.c $(DEPS)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

ht_test: $(LIBNAME) ./testy/test_main.o test.o
	$(CC) -o $@ $^ $(LFLAGS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS) $(LIBNAME) test.o

