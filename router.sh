#!/bin/bash



cd ~/framework/dpdk
make config T=x86_64-native-linuxapp-gcc
make T=x86_64-native-linuxapp-gcc

modprobe uio
insmod build/kmod/igb_uio.ko

usertools/dpdk-devbind.py --bind=igb_uio eth1
usertools/dpdk-devbind.py --bind=igb_uio eth2
usertools/dpdk-devbind.py --bind=igb_uio eth3 

mount -t hugetlbfs nodev /mnt/huge
echo 256 > ~/../sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages



cd ..
cmake .
make 



