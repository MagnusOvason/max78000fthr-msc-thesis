#!/bin/sh
DEVICE="MAX78000"
TARGET="sdk/Examples/$DEVICE/CNN"
COMMON_ARGS="--device $DEVICE --timer 0 --display-checkpoint --verbose --overwrite"
BOARD="FTHR_RevA"

python ai8xize.py --test-dir $TARGET --prefix cifar-10-max78000fthr-nas --board-name $BOARD --checkpoint-file trained/ai85-cifar10-new-q.pth.tar --config-file networks/cifar10-nas.yaml --fifo --softmax $COMMON_ARGS "$@"
python ai8xize.py --test-dir $TARGET --prefix cifar-10-max78000fthr-riscv-nas --board-name $BOARD --checkpoint-file trained/ai85-cifar10-new-q.pth.tar --config-file networks/cifar10-nas.yaml --fifo --softmax $COMMON_ARGS "$@" --riscv --riscv-debug
python ai8xize.py --test-dir $TARGET --prefix cifar-10-max78000fthr-riscv-nas-fastfifo --board-name $BOARD --checkpoint-file trained/ai85-cifar10-new-q.pth.tar --config-file networks/cifar10-nas.yaml --fast-fifo --softmax $COMMON_ARGS "$@" --riscv --riscv-debug