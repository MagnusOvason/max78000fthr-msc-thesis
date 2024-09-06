#!/bin/sh
python train.py --model ai85cdnet --dataset cats_vs_dogs --confusion --evaluate --exp-load-weights-from ../ai8x-synthesis/trained/ai85-catsdogs-new-q.pth.tar -8 --summary onnx_simplified --summary-filename ai85-catsdogs-new-simplified --device MAX78000 "$@"
