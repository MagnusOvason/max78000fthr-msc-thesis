#!/bin/sh
python train.py --model ai85nascifarnet --dataset CIFAR10 --evaluate --device MAX78000 --exp-load-weights-from ../ai8x-synthesis/trained/ai85-cifar10-new-q.pth.tar -8 --use-bias "$@"
