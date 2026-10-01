// Example Phlex workflow: DUNE DAQ HDF5 -> WCT frame file (npz/tar/tar.gz/zip).
//
// This is the reference wiring the dune-daq-phlex source + driver were built
// for.  It is a drop-in analog of phlex-arrow-hdf's integration_read.jsonnet:
// only the source+driver differ (a DUNE DAQ HDF5 reader instead of an Arrow
// HDF5 reader); the downstream convert + sink are identical.
//
//   dune_daq_phlex_driver  (drives exactly the DAQ file's trigger records)
//     -> dune_daq_phlex_source          (DAQ HDF5 -> decode -> Arrow "wc.frame")
//       -> wire_cell_phlex_arrow_from_arrow  (TableGroup -> wcphlex::Frame)
//         -> wcph_frame_sink             (WCT FrameFileSink -> the output file)
//
// The driver receives the SAME DaqFrameSource instance (named in uses_sources)
// and yields exactly the "event" cells that source found by scanning the DAQ
// file -- one per trigger record, with NO preconfigured count (the "collusion").
// from_arrow is the CONSUMER that creates demand for the source's product (a
// Phlex provider only runs when something demands its product); the sink is the
// end-cap that creates demand for the reconstructed frame.
//
// `phlex` cannot yet compile its own Jsonnet, so drive this with `phlexed`
// (which honors PHLEXED_PATH + -J/--jpath and TLAs) or compile to JSON first.
//
// The three placeholders (<...>) are what a wrapper script fills per invocation:
//   in_file      : the DUNE DAQ HDF5 file (positional).
//   channel_map  : the DUNE ChannelMap*.txt matching the detector
//                  (np04hd/PDHD -> pd2hd/PD2HDChannelMap_v*.txt;
//                   np02vd/PDVD -> pd2vd/PD2VD*ChannelMap_v*.txt).
//   out_file     : the WCT frame file (.npz/.tar/.tar.gz/.zip).
{
  driver: {
    cpp: 'dune_daq_phlex_driver',
    uses_sources: ['daq'],  // drive the trigger records this source found
    layers: ['event'],      // layer shape only; the source supplies the cells
  },
  sources: {
    daq: {
      cpp: 'dune_daq_phlex_source',
      input_file: '<in_file>',        // DUNE DAQ HDF5
      channel_map: '<channel_map>',   // DUNE ChannelMap*.txt for the detector
      tick: 0.5e-6,                   // sample period (s); detector config, not in payload
      product: 'frame',
      output_creator: 'input',        // label on the emitted TableGroup
      output_layer: 'event',
      // Optional record slice (default: all records).  first_record is 0-based;
      // max_records <= 0 means all remaining.  Handy on multi-GB files.
      first_record: 0,
      max_records: 0,
    },
  },
  modules: {
    from_arrow: {
      cpp: 'wire_cell_phlex_arrow_from_arrow',
      input_creator: 'input',   // matches the source's output_creator
      input_layer: 'event',
      types: ['frame'],
    },
    frame_sink: {
      cpp: 'wcph_frame_sink',
      wct_config: 'frame-file-sink.jsonnet',            // from wire-cell-phlex (WIRECELL_PATH)
      wct_plugins: ['WireCellPgraph', 'WireCellSio'],
      // Consume the frame the convert node produced.  In Phlex 0.3.2 a
      // transform's output creator is its own module label (the instance name)
      // and its layer is the input's layer: (from_arrow, event, frame).
      inputs: [{ creator: 'from_arrow', layer: 'event', suffix: 'frame' }],
      outputs: [],
      wct_tla: { outname: '<out_file>' },
    },
  },
}
