#!/bin/sh
python train.py --epochs 200 --optimizer Adam --lr 0.001 --wd 0 --deterministic --compress policies/schedule-catsdogs.yaml --qat-policy policies/qat_policy_cd.yaml --model ai85cdnet --dataset cats_vs_dogs --enable-tensorboard --confusion --pr-curves --param-hist --embedding --device MAX78000 "$@"
# The above line does the following:
# 1. Train for 200 epochs
# 2. Use the Adam optimizer with a learning rate of 0.001 and weight decay of 0
# 3. Use a deterministic configuration (Seed random number generators with fixed values)
# 4. Use the compression schedule defined in policies/schedule-catsdogs.yaml, which is a quantization-aware training (QAT) schedule
# 5. Use the quantization policy defined in policies/qat_policy_cd.yaml, which is a quantization-aware training (QAT) policy
# 6. Use the ai85cdnet model
# 7. Use the cats_vs_dogs dataset
# 8. Print the confusion matrix
# 9. Save the parameter histogram
# 10. Save the embeddings
# 11. Use the MAX78000 device

# Quantization-aware training is the better performing approach. 
# QAT learns additional parameters during training that help with quantization.
# The quantization policy is defined in policies/qat_policy_cd.yaml.