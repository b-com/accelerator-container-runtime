.PHONY: all clean

CC=gcc

#DEBUG_CFLAGS := -DXILINX_DEBUG
DEBUG_CFLAGS :=

BIN_NAME := accelerator-container-runtime-tool
BIN_SRCS := $(wildcard *.c)
BIN_INCLUDES= $(wildcard *.h)
BIN_OBJS := $(BIN_SRCS:.c=.o)
BIN_CFLAGS  = -std=gnu99 -z noexecstack -z relro -z now -fPIE -fPIC -pie -O2 \
              -D_GNU_SOURCE -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Wall -Wno-unused-result \
             -I../../aws-fpga/sdk/userspace/include $(DEBUG_CFLAGS) $(CFLAGS)
BIN_LDFLAGS = -rdynamic $(LDFLAGS)
BIN_LDLIBS  = -lpthread -lm -ljson-c -ldl
#BIN_LDLIBS  =  -luuid -lopae-c

all: $(BIN_NAME) 

$(BIN_NAME): $(BIN_OBJS)
	$(CC) $(BIN_CFLAGS) $(BIN_LDFLAGS) $^ -o $(BIN_NAME) $(BIN_LDLIBS)

$(BIN_OBJS): %.o: %.c $(BIN_INCLUDES)
	$(CC) $(BIN_CFLAGS) -MMD -MF $*.d -c $<

clean:
	rm -rf *.d *.o $(BIN_NAME)
