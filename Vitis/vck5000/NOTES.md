
-----------------------------------------------------
11/14/2023
### Attempted to write a single kernel which could compute a+/- b+/- and c+/-

But got this error:

```
Error in "/lustre/home/nx08/nx08/searsm8/AIEplace/Vitis/vck5000/aie/src/compute_hpwl.cpp", line 18: (approximate): variable length array 'a_minus' size computation requires control flow, not supported
*** fatal error -- giving up
```

This is because the number of a+/- that must be computed can vary based on the netsize being processed. This is worked around for data input by first reading in 2 control values (netsize and netcount) But for storing all the a+/- values we need an array of varied size, which does not compile due to this error.

--> Therefore, we will split the a+/- kernel from the others

-----------------------------------------------------
### For AIE kernels, the maximum number of input/output streams is 2.

```ERROR: [aiecompiler 77-4280] src/hpwlGraph.h:18:19: 'adf::kernel::create': The AI Engine kernel has 3 input stream ports, but the maximum number of input stream ports that an AI Engine kernel can have is 2.
ERROR: [aiecompiler 77-4281] src/hpwlGraph.h:18:19: 'adf::kernel::create': The AI Engine kernel has 4 output stream ports, but the maximum number of output stream ports that an AI Engine kernel can have is 2.```

--> Therefore, should re-think bc kernel, I will try input/output buffers 

apm kernel can use streams (to work around the lack of runtime parameters on vck5000), then output ping-pong buffer to bcpm kernel.