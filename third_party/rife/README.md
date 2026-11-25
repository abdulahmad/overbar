# RIFE 4.6 (frame generation)

`rife-v4.6/flownet.param` and `rife-v4.6/flownet.bin` are the RIFE 4.6 model as converted to ncnn by
rife-ncnn-vulkan (release 20221029, https://github.com/nihui/rife-ncnn-vulkan), unchanged. Overbar reads
them at run time with its own C code (`src/framegen.c`); nothing of rife-ncnn-vulkan or ncnn is compiled in.

- The model: RIFE, Practical-RIFE v4.6 by hzwer (https://github.com/hzwer/Practical-RIFE), MIT License,
  Copyright (c) 2021 hzwer. Text: [LICENSE-RIFE.txt](LICENSE-RIFE.txt).
- The ncnn conversion: rife-ncnn-vulkan, MIT License, Copyright (c) 2020 nihui.
  Text: [LICENSE-rife-ncnn-vulkan.txt](LICENSE-rife-ncnn-vulkan.txt).

A port copies `rife-v4.6/` next to its program with `$(call overbar_copy_rife,DIR)` (overbar.mk).
