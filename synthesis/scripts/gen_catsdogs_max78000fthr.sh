#!/bin/sh
DEVICE="MAX78000"
TARGET="sdk/Examples/$DEVICE/CNN"
COMMON_ARGS="--device $DEVICE --timer 0 --display-checkpoint --verbose --overwrite"
BOARD="FTHR_RevA"

python ai8xize.py --test-dir $TARGET --prefix cats-dogs-max78000fthr --board-name $BOARD --checkpoint-file trained/ai85-catsdogs-new-q.pth.tar --config-file networks/cats-dogs-hwc.yaml --fifo --softmax $COMMON_ARGS "$@"
python ai8xize.py --test-dir $TARGET --prefix cats-dogs-max78000fthr-riscv --board-name $BOARD --checkpoint-file trained/ai85-catsdogs-new-q.pth.tar --config-file networks/cats-dogs-hwc.yaml --fifo --softmax $COMMON_ARGS "$@" --riscv --riscv-debug
python ai8xize.py --test-dir $TARGET --prefix cats-dogs-max78000fthr-riscv-fastfifo --board-name $BOARD --checkpoint-file trained/ai85-catsdogs-new-q.pth.tar --config-file networks/cats-dogs-hwc.yaml --fast-fifo --softmax $COMMON_ARGS "$@" --riscv --riscv-debug