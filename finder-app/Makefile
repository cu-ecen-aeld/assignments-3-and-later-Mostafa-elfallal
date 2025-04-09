# If CROSS_COMPILE is provided, append gcc to it. Otherwise, default to gcc.
CC := $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra

# Object file and target application
OBJ = writer.o
TARGET = writer

# Default target: compile writer.c into writer.o and link to create the writer app
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

$(OBJ): writer.c
	$(CC) $(CFLAGS) -c writer.c -o $(OBJ)

# Clean up generated files
clean:
	rm -f $(OBJ) $(TARGET)

