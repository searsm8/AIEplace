# pl_algo data flow

One placement iteration, as data moves through the PL modules. Byte/beat layouts are
defined precisely in `src/formats.hpp`; this file is the narrative. All transport is
128-bit aligned (4 floats per DDR beat / per AXI-stream beat).

Hardware grid is **1024 x 1024**. Each real matrix (bin density, Ex, Ey) is 4 MB and each
complex FFT scratch matrix is 8 MB, so all matrices are **DDR-resident** and streamed
through the PL in row tiles; on-chip BRAM/URAM holds only the working tiles.

## Stage-by-stage

| # | Stage | Producer -> Consumer | Format |
|---|-------|----------------------|--------|
| 1 | Node coords | (Memory Writer / host) -> HPWL Mgr, Density Mgr | DDR, 1 beat/node `{x,y,_,_}` |
| 2 | HPWL gradient | HPWL Mgr -> Iteration Update | DDR, 1 beat/node `{gx,gy,_,_}` (scatter-accumulated) |
| 2a| HPWL packet | HPWL Mgr <-> AIE HPWL graph | stream: pin coords out `{x,y,...}`, partials back `{dW/dx,dW/dy,...}` |
| 3 | Bin density | Density Mgr (internal) | DDR, 1024x1024 real, 256 beats/row |
| 3a| FFT I/O | Density Mgr PL pre/post <-> AIE FFT pool | stream cfloat `{re,im,re,im}`, 512 beats/row, 8 lanes |
| 3b| E-field Ex,Ey | Density Mgr -> Iteration Update | DDR, 1024x1024 real each, 256 beats/row |
| 4 | Updated coords | Iteration Update -> Memory Writer | stream, 1 beat/node `{x,y,_,_}` |
| 5 | Nesterov state | Iteration Update (read-modify-write) | DDR, 2 beats/node `{ux,uy,vx,vy}`,`{pgx,pgy,_,_}` |
| 6 | Status | Metrics -> host | DDR, 1 beat `{hpwl,overflow,_,_}` |

## Control / policy (v1)
The host owns the gamma schedule, the lambda update (and "jolt"), and the convergence
test. It passes `gamma`, `lambda`, `alpha` into each `top` invocation over AXI-Lite and
reads the status beat back between iterations. The on-PL iteration loop is retained so
this policy can later migrate onto the PL (gamma becomes a ROM indexed by overflow) with
no change to the datapath.

## Open format decisions (to finalize as modules are implemented)
- AoS vs SoA and 1-vs-2 nodes per beat for the coord/gradient buffers.
- Exact net packet grouping for the AIE HPWL graph (mirror markv1 `prepareNetGroup`).
- Final AIE PLIO port names for the FFT pool and HPWL graph (with `aie/src/pl_algo`).
- IDXST path (Ey) -- deferred; a tweak on the IDCT flow.
