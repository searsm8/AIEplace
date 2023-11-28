
11/14/2023
### Attempted to write a single kernel which could compute a+/- b+/- and c+/-

But got this error:

```Error in "/lustre/home/nx08/nx08/searsm8/AIEplace/Vitis/vck5000/aie/src/compute_hpwl.cpp", line 18: (approximate): variable length array `a_minus' size computation requires control flow, not supported
*** fatal error -- giving up```

This is because the number of a+/- that must be computed can vary based on the netsize being processed. This is worked around for data input by first reading in 2 control values (netsize and netcount) But for storing all the a+/- values we need an array of varied size, which does not compile due to this error.

--> Therefore, we will split the a+/- kernel from the others