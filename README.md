# dune-daq-phlex

A Phlex **read source** (and its colluding **driver**) for DUNE DAQ HDF5 data.
Loading the plugin registers a `phlex::source` that navigates a raw DUNE DAQ
HDF5 file, decodes its TPC ADC fragments, and emits — one per DAQ trigger
record — an Arrow **"wc.frame"** product (`phlex_arrow::TableGroup` with member
tables `traces`/`frame_tags`/`trace_tags`/`cmm`).  That is the same product the
rest of the Phlex/WCT chain already consumes: a downstream
`wire_cell_phlex_arrow_from_arrow` node turns it back into a WCT frame for
signal processing or a `FrameFileSink`.

Tracked by beads **ddm-3j8.1.6** (Phlex source for DUNE DAQ HDF5 input);
un-stubs the `daq-hdf` input kind that `dune-config`'s
`cfg/dune/phlex/sources.jsonnet` carried as a placeholder.

## What it reuses

This package is thin glue.  The reading, decoding, channel mapping, and Arrow
framing are the existing, tested pieces:

| stage | package | API used |
|---|---|---|
| read raw HDF5 | `dune-daq-hdf` | `DaqHdf5File::records()/fragments()/read_bytes()` |
| decode → dense ADC | `dune-daq-codec` | `decode()`, `DecodedFragment` |
| online → offline channel | `dune-daq-codec` | `OnlineOfflineChannelMap::offline()` |
| decoded → WCT `IFrame` | (`dune-daq-arrow-frame-hdf` `ToFrame`) | `traces_from()`/`make_frame()` — reproduced (~25 lines; that package exports no linkable target) |
| `IFrame` → Arrow tables | `wire-cell-arrow` | `WireCell::Arrow::to_arrow_sparse()` |
| the Phlex product | `phlex-arrow-common` | `phlex_arrow::TableGroup{type="wc.frame", ...}` |

The end-to-end chain mirrors the non-Phlex `daq_hdf_to_arrow` tool in
`dune-daq-arrow-frame-hdf`, but delivered as an in-graph Phlex source instead of
a standalone program.

## Source + driver (Phlex >= 0.4.1)

Phlex (since 0.3.2) supports the source/driver model as first-class features,
so this package uses it (rather than the `PHLEX_REGISTER_PROVIDERS` +
`generate_layers` fallback):

- **`DaqFrameSource`** (`PHLEX_REGISTER_SOURCE`, plugin `dune_daq_phlex_source`)
  owns the open DAQ file + parsed channel map, advertises the `wc.frame`
  TableGroup via `create_providers()`, and enumerates one `event` data cell per
  trigger record via `indices()`/`cells()`.
- **`dune_daq_phlex_driver`** (`PHLEX_REGISTER_DRIVER`) receives that same source
  object (named in `driver.uses_sources`) and yields exactly the cells the
  source discovered — the **collusion**: the record count comes from the file at
  run time, never from config.  The job supplies only the layer *shape*
  (`layers: ['event']`).

> Ported to Phlex 0.4.1: the source uses the public `phlex::source` /
> `phlex::provider_bundles` and phlex-arrow-common's `provide_if_selected()`
> (no `phlex::detail`), and the installed `phlex/driver.hpp` (the vendored
> stopgap copy is gone).  Phlex 0.4 requires implicit providers to carry a real
> stage, so the source has a `stage` key (default `daq`), and jobs must set a
> top-level `stage`.

## Plugin config keys

`sources.<name>` (the `<name>` is what a driver's `uses_sources` refers to):

| key | req? | default | meaning |
|---|---|---|---|
| `input_file` | yes | — | the DUNE DAQ HDF5 file |
| `channel_map` | yes | — | a DUNE `ChannelMap*.txt` (12-/13-col, auto-detected) for the detector |
| `tick` | no | `0.5e-6` | sample period (s); detector config, not in the DAQ payload |
| `product` | no | `frame` | emitted product suffix |
| `output_creator` | no | `input` | creator label on the emitted product |
| `output_layer` | no | `event` | Phlex layer the product + its data cell live in |
| `stage` | no | `daq` | stage the emitted product carries (not `CURRENT`) |

`driver`:

| key | req? | meaning |
|---|---|---|
| `cpp` | yes | `dune_daq_phlex_driver` |
| `uses_sources` | yes | the source name(s) to drive (exactly one `DaqFrameSource`) |
| `layers` | yes | layer path job→leaf, e.g. `['event']`; must match the source's `output_layer` |

## Example workflow

See `test/daq-to-frame-file.jsonnet` — the reference DAQ-HDF5 → WCT frame-file
graph (`dune_daq_phlex_source` → `wire_cell_phlex_arrow_from_arrow` →
`wcph_frame_sink`).  Drive it with **`phlexed`** (not `phlex`, which cannot yet
compile its own Jsonnet); `phlexed` honors `PHLEXED_PATH` and `-J/--jpath` so
`import`s and the `frame-file-sink.jsonnet` (from wire-cell-phlex) resolve.

Channel map for the detector:
`np04hd`/PDHD → `dune-daq-codec/data/channelmaps/pd2hd/PD2HDChannelMap_v*.txt`;
`np02vd`/PDVD → `.../pd2vd/PD2VD*ChannelMap_v*.txt`.

## Build

A pure-plugin package (no exported CMake Config; loading the plugin is its only
interface).  Built by the `devel/` superbuild automatically (auto-discovered by
its `project(dune_daq_phlex)` name, ordered after the siblings it
`find_package()`s):

```bash
cmake -S devel -B builds/devel -DCMAKE_PREFIX_PATH=<install>;<spack-view>
cmake --build builds/devel
```

Standalone against the installed siblings + Spack view:

```bash
VIEW=$PWD/extern/envs/gcc15/view
INST=$PWD/installs/envs/gcc15
cmake -S devel/dune-daq-phlex -B builds/envs/gcc15/dune-daq-phlex -G Ninja \
  -DCMAKE_CXX_COMPILER=$VIEW/bin/g++ \
  -DCMAKE_PREFIX_PATH="$INST;$VIEW" -DCMAKE_INSTALL_PREFIX=$INST
cmake --build builds/envs/gcc15/dune-daq-phlex
cmake --install builds/envs/gcc15/dune-daq-phlex
```

Installs three libraries to `<prefix>/lib`: `libdune_daq_phlex.so` (impl) and
the two dlopen plugins `libdune_daq_phlex_source.so` /
`libdune_daq_phlex_driver.so` (found via `PHLEX_PLUGIN_PATH`).

## Status

Built, installed, and the plugins export the expected `create_source` /
`create_driver` entry points with all shared-library dependencies resolving.
An end-to-end run needs a real DUNE DAQ HDF5 file + the matching channel map
(not vendored here); that integration lives in `dune-config` (beads
ddm-g3r.5 / ddm-3j8.1.14 for channel-map delivery as a Phlex resource).
