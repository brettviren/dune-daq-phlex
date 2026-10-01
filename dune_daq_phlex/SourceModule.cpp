// Phlex plugin: register the DUNE DAQ HDF5 read source (beads ddm-3j8.1.6).
//
// Loading this plugin registers a DaqFrameSource (a phlex::source) under the
// job's "sources" config section.  The framework calls the source's
// create_providers() to implicitly wire a provider that decodes each DAQ
// trigger record into an Arrow "wc.frame" product (phlex_arrow::TableGroup);
// downstream, a wire-cell-phlex-arrow convert node turns that back into a WCT
// frame product for signal processing / a FrameFileSink.  The source also
// enumerates one "event" data cell per DAQ record (indices()/cells()), which
// the colluding file-driven driver (dune_daq_phlex_driver) drives.
//
// Config keys (under "sources"):
//   input_file     (string, required): the DUNE DAQ HDF5 file to read.
//   channel_map    (string, required): a DUNE ChannelMap*.txt (12- or 13-col,
//                  auto-detected) matching the detector of input_file.
//   tick           (real, optional, default 0.5e-6): sample period in WCT
//                  seconds (detector configuration; not in the DAQ payload).
//   product        (string, optional, default "frame"): the emitted product
//                  suffix.
//   output_creator (string, optional, default "input"): the creator label on
//                  the emitted product (what downstream nodes route on).
//   output_layer   (string, optional, default "event"): the Phlex layer the
//                  product (and its data cell) lives in.
//   first_record   (int, optional, default 0): 0-based file-order index of the
//                  first trigger record to process.
//   max_records    (int, optional, default 0): max records to process from
//                  first_record; <= 0 means all remaining.
//   stage          (string, optional, default "daq"): the stage the emitted
//                  product carries.  Phlex >= 0.4 rejects the reserved "CURRENT".

#include "dune_daq_phlex/DaqFrameSource.hpp"

#include "phlex/configuration.hpp"
#include "phlex/source.hpp"

#include <string>

PHLEX_REGISTER_SOURCE(s, config)
{
    auto const input_file = config.get<std::string>("input_file");
    auto const channel_map = config.get<std::string>("channel_map");
    auto const tick = config.get<double>("tick", 0.5e-6);
    auto const product = config.get<std::string>("product", std::string{"frame"});
    auto const output_creator = config.get<std::string>("output_creator", std::string{"input"});
    auto const output_layer = config.get<std::string>("output_layer", std::string{"event"});
    auto const first_record = config.get<int>("first_record", 0);
    auto const max_records = config.get<int>("max_records", 0);
    auto const stage = config.get<std::string>("stage", std::string{"daq"});

    // Register under the source's configuration label (the "sources" section
    // key, supplied by the framework as module_label) so a driver's
    // `uses_sources` can refer to this source by that name.
    auto const label = config.get<std::string>("module_label", std::string{"daq_read"});

    s.add_source<dune_daq_phlex::DaqFrameSource>(
      label, input_file, channel_map, tick, output_creator, product, output_layer,
      first_record, max_records, stage);
}
