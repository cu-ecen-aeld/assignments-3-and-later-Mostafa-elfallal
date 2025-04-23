# this should use native compiler if there is no external compiler
# should clean compilation artifacts 
#  the compiled file is aesdsocket.c into aesdsocket
CC ?= gcc
CFLAGS ?= -Wall -Wextra
LDFLAGS ?= -lpthread
TARGET = aesdsocket
SRCS = aesdsocket.c
OBJS = $(SRCS:.c=.o)
# this is the default target
all: $(TARGET)
# this is the target to build the aesdsocket executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) -c $(SRCS) -o $(OBJS)

# this is the target to clean up the build artifacts
clean:
	rm -f $(OBJS) $(TARGET)
