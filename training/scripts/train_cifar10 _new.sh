#!/bin/sh
python train.py --deterministic --epochs 300 --optimizer Adam --lr 0.001 --wd 0 --compress policies/schedule-cifar-nas.yaml --model ai85nascifarnet --dataset CIFAR10 --enable-tensorboard --confusion --pr-curves --param-hist --embedding --device MAX78000 --batch-size 100 --print-freq 100 --validation-split 0 --use-bias --qat-policy policies/qat_policy_late_cifar.yaml --confusion "$@"
# The above line does the following:
# Train for 300 epochs
# Use the Adam optimizer with a learning rate of 0.001 and weight decay of 0
# Use a deterministic configuration (Seed random number generators with fixed values)
# Use the compression schedule defined in policies/schedule-cifar-nas.yaml
# Use the ai85nascifarnet model
# Use the CIFAR10 dataset
# Print the confusion matrix
# Save the parameter histogram
# Save the embeddings
# Use the MAX78000 device
# Use a batch size of 100, meaning that 100 images are processed before the weights are updated
# Print the frequency of 100, meaning that the training loss is printed every 100 batches
# Use the validation split of 0, meaning that no validation is performed
# Use bias
# Use the quantization-aware training policy defined in policies/qat_policy_late_cifar.yaml