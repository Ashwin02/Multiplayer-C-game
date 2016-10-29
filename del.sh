#!/bin/sh

killall -9 test_prg
rm /dev/shm/sem*
rm /dev/shm/ashwinsGoldMem*
rm /dev/mqueue/*
tput init
stty sane
make clean
make
