EXEC = cyclicping

SRC = cyclicping.c socket.c tcp.c udp.c ftrace.c opts.c stats.c uart.c stsn.c
INC = cyclicping.h socket.h tcp.h udp.h ftrace.h opts.h stats.h uart.h stsn.h

ifdef NETMAP
SRC += netmap.c
INC += netmap.h
DEFINES += -DHAVE_NETMAP
NETMAP_INCLUDE = -I$(NETMAP)/sys
endif

PSRC = $(addprefix src/,$(SRC))
OBJS := $(patsubst %.c,%.o,$(PSRC))
INCLUDES = $(addprefix src/,$(INC))

CFLAGS += -Wall -std=gnu99 -fgnu89-inline -Isrc $(NETMAP_INCLUDE) $(DEFINES)
LDLIBS += -lrt -lm

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f $(EXEC) $(OBJS)
