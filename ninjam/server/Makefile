CFLAGS = -O2 -Wall -Wno-reorder

ifdef MAC
CFLAGS += -D_MAC
else
CFLAGS += -pthread
endif

CC=gcc
CXX=g++
CXXFLAGS = $(CFLAGS)

OBJS = ../../WDL/jnetlib/asyncdns.o
OBJS += ../../WDL/jnetlib/connection.o
OBJS += ../../WDL/jnetlib/listen.o
OBJS += ../../WDL/jnetlib/util.o
OBJS += ../../WDL/jnetlib/httpget.o
OBJS += ../../WDL/rng.o
OBJS += ../../WDL/sha.o
OBJS += ../mpb.o
OBJS += ../netmsg.o
OBJS += usercon.o
OBJS += ninjamsrv.o


default: ninjamsrv

ninjamsrv: $(OBJS)
	$(CXX) $(CXXFLAGS) -s -o ninjamsrv $(OBJS)

clean:
	-rm $(OBJS) ninjamsrv
