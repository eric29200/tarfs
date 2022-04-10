#!/bin/csh

./unload.csh
make
sudo insmod tarfs.ko
mkdir mnt
sudo mount ./test.tar -o loop -t tarfs mnt
