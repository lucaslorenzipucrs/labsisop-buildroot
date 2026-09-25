BUILDROOT_DIR := ..
COMPILER := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc

all:
	$(COMPILER) -o disk_test disk_test.c
	cp disk_test $(BUILDROOT_DIR)/output/target/bin

clean:
	rm -f disk_test
