#!/bin/csh

./unload.csh
make
sudo insmod tarfs.ko
sudo mount ./test.tar -o loop -t tarfs mnt
