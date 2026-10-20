# Runtime configuration

Simphony applications use a named JSON file as the canonical runtime
configuration. Pass the basename with `-c` or `--config`; for example,
`-c wls_test` loads `wls_test.json`. Files are searched in the installed
configuration directories, or only in `SIMPHONY_CONFIG_DIR` when that
environment variable is set.

```bash
SIMPHONY_CONFIG_DIR=$PWD/config \
    GPUPhotonSourceMinimal \
    -g tests/geom/wls_test.gdml \
    -c wls_test \
    -m tests/run.mac \
    -s 42
```

The public typed representation is `simphony::Config` in
`sysrap/config.h`. `Config` parses the selected file and applies event values
through `SEventConfig`, which remains the process-wide low-level facade used
by Opticks code.

## Defaults and discovery

An event setting is selected from an explicit key in the chosen JSON file or,
when omitted, from the typed application default documented below. `Config`
applies the complete result to the process-wide `SEventConfig` facade before
event creation.

`SIMPHONY_CONFIG_DIR` and `SIMPHONY_PTX_DIR` are discovery variables, not
event settings, and remain supported.

Application entry points must construct `Config` before creating an `SEvt`
or initializing the Simphony runtime. The DD4hep `OpticsRun` action does so
once, at the first begin-of-run, from its `ConfigName` property. Its default
basename is `dd4hep_gpu`; attempting to select another configuration after
initialization is rejected because `SEventConfig` is process-wide.

## Event schema

All keys in this table belong to the top-level `event` object. Defaults are
the effective application values used when JSON omits a key.

| JSON key | Type and default | Purpose |
|----------|------------------|---------|
| `event_mode` | name, `Minimal` | Selects GPU-to-CPU gathering and CPU-to-file persistence. Accepted names are `DebugHeavy`, `DebugLite`, `Nothing`, `Minimal`, `Hit`, `HitPhoton`, `HitPhotonSeq`, and `HitSeq`. The old `mode` spelling is not an alias. |
| `integration_mode` | integer, `-1` | Selects Opticks/framework integration: `-1` disabled, `1` GPU, `2` CPU, `3` both. |
| `running_mode` | name, `SRM_DEFAULT` | Selects the source or Geant4 state workflow. Accepted values are the `SRM_*` names understood by `SRM`. |
| `start_index` | integer, `0` | Offset added to externally visible and persisted event indices. |
| `num_event` | integer, `1` | Event count used by low-level multi-event helpers. Application run managers can have their own event-count control. |
| `event_skipahead` | integer, `100000` | Number of random values skipped between event indices. |
| `g4_state_spec` | string, `1000:38` | Geant4 random-state allocation specification. |
| `g4_state_rerun` | integer, `-1` | Photon index selected when `running_mode` enables a saved-state rerun. |
| `max_bounce` | integer, `31` | Maximum optical-photon bounce count. |
| `max_genstep` | integer, `10000000` | Maximum number of uploaded gensteps. |
| `maxslot` | integer, `0` | Maximum CUDA launch slots. Zero selects the VRAM-based heuristic. The historical `maxslot` spelling is retained in JSON. |
| `max_simtrace` | integer, `1000000000000` | Maximum number of simtrace records allocated for a launch. |
| `max_time` | number, `1e27` ns | Propagation time cutoff; the default is effectively unlimited. |
| `mode_lite` | name, `Standard` | Photon/hit representation: `Standard`, `Lite`, or `DebugCompare`. |
| `mode_merge` | name, `Separate` | Hit representation: `Separate` or `Merged`. |
| `merge_window` | number, `0` ns | Time window used when merged hit output is enabled. |
| `output_dir` | path, current working directory | Base directory for event folders and relative timing outputs. Low-level tools that do not construct `Config` retain their own `$DefaultOutputDir` convention. |
| `event_reldir` | string, `ALL${VERSION:-0}` | Relative directory below `output_dir` used for event arrays. |
| `hit_mask` | string, `SD` | Comma-delimited optical flag abbreviations selecting photons copied to hit output. |
| `propagate_epsilon` | number, `0.05` mm | Ray-origin offset after a boundary crossing. |
| `propagate_epsilon0` | number, `0.05` mm | Ray-origin offset after a bulk interaction. |
| `propagate_epsilon0_mask` | string, `TO,CK,SI,SC,RE` | Interactions that use `propagate_epsilon0`. |
| `propagate_refine` | unsigned integer, `0` | Bit mask enabling propagation-refinement diagnostics. |
| `propagate_refine_distance` | number, `5000` mm | Maximum distance considered by propagation refinement. |
| `input_genstep` | string, empty | Input genstep `.npy` path or indexed format pattern. |
| `input_photon` | string, empty | Input photon `.npy` path. |
| `input_photon_frame` | string, empty | Geometry frame specification used to transform input photons. |
| `input_photon_change_time` | number, `-1` ns | Replacement time for input photons; a negative value preserves stored times. |
| `input_photon_record_time` | string, empty | Time-selection expression when photons are sourced from a record array. |
| `input_photon_record_slice` | string, empty | History slice expression when photons are sourced from a record array. |

The `torch` object configures generated optical photons, including
`numphoton`, position, direction, polarization, wavelength, and source shape.
The low-level `SRM_TORCH` workflow creates one genstep per event from these
same typed settings. Invalid `running_mode` names are rejected while loading
the profile, with the accepted names included in the error.
The `event_timing` object configures the per-event timeline and detailed
profile CSV outputs. See [Simulation inputs and outputs](inputs-outputs.md)
and [Performance and debugging](performance-and-debugging.md) for those
workflows.

## Example

```json
{
  "event": {
    "event_mode": "HitPhoton",
    "integration_mode": 1,
    "max_bounce": 100,
    "max_genstep": 10000000,
    "maxslot": 100000,
    "max_time": 1000000.0,
    "mode_lite": "Standard",
    "mode_merge": "Separate",
    "output_dir": "./output",
    "event_reldir": "wls-validation",
    "hit_mask": "SD",
    "propagate_epsilon": 0.05,
    "propagate_epsilon0": 0.05,
    "propagate_epsilon0_mask": "TO,CK,SI,SC,RE"
  }
}
```

Use `HitPhoton` for persisted final photons and hits, `HitPhotonSeq` when
`seq.npy` is also required, and `DebugLite` when step-by-step `record.npy`
output is required. Debug modes derive their component masks and diagnostic
buffer maxima; those internals are not independent configuration knobs.
