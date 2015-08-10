obj-m :=memory.o
memory-y := mem_rdma.o mem_hash.o mem_driver.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	rm -f *.o
	rm -f *.ko
	rm -f *.mod.c
	rm -f Module.symvers
	rm -f Module.markers
