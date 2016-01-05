
DPDK=/home/simon/src_packages/odp_inst

GCC=gcc
CFLAGS=-L$(DPDK)/lib -I$(DPDK)/include

stack += src/stack/eth.o
stack += src/stack/arp.o

test:
	echo $(CFLAGS)

src/stack/%.o: src/stack/%.c
	$(GCC) -c $(CFLAGS) $< -o $@

build: $(stack)
	rm $(stack)



