# Async GPU Launch Example

Demonstrates asynchronous CPU+GPU optical photon simulation using
double-buffered genstep collection with Geant4's `G4TaskGroup`.

## Architecture

```
CPU Event Loop                    GPU Worker (G4TaskGroup)
-----------------                 ------------------------
Event N:                          
  EM shower -> Cerenkov/Scint     
  Collect gensteps into buffer A  
  Buffer A hits threshold         
  Swap A <-> B                    
  Submit buffer A to GPU -------> Process buffer A:
Event N+1:                          Load gensteps into SEvt
  Collect gensteps into buffer B    gx->simulate()
  ...                               Save hits
                                    Done
End of run:
  Flush buffer B ----------------> Process buffer B
  Wait for completion              Done
```

The CPU never blocks waiting for the GPU (except at end-of-run flush).
A single `G4Mutex` ensures only one GPU kernel runs at a time.

## Modes

| Flag     | Behavior |
|----------|----------|
| `--async` | (default) Double-buffered async GPU processing |
| `--sync`  | Original end-of-run batch GPU simulation |

## Configuration

The example constructs `simphony::Config` before creating its event state.
Use `-c NAME` to load `NAME.json`; the default `async_gpu` profile sets
`event.event_mode` to `Minimal` and `event.max_bounce` to 1000. See
[`docs/configuration.md`](../../docs/configuration.md) for JSON discovery and
the complete event schema.

`GPU_PHOTON_FLUSH_THRESHOLD` remains an example-specific environment setting
because it controls batching rather than Simphony event configuration.

| Variable | Default | Description |
|----------|---------|-------------|
| `GPU_PHOTON_FLUSH_THRESHOLD` | 10000000 | Photons per GPU batch |

## Build (standalone)

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/simphony/install
make
```

## Build (in-tree)

The example is also built as part of the main simphony build.

## Run

```bash
# Async mode (default)
./run.sh

# Sync mode
./run.sh --sync

# Custom threshold (smaller batches = more CPU/GPU overlap)
GPU_PHOTON_FLUSH_THRESHOLD=1000000 ./run.sh
```

## Output

- `gpu_hits_batch_*.npy` — GPU hits per batch (async mode)
- `gpu_hits.npy` — GPU hits (sync mode)
- `g4_hits.npy` — Geant4 reference hits

All hit files use the sphoton layout: `(N, 4, 4)` float32 array with
fields `pos/time`, `mom`, `pol/wavelength`, `flags`.

## Supported Physics

Both Cerenkov and scintillation gensteps are collected. Multi-component
scintillation (up to 3 time constants) is supported.
