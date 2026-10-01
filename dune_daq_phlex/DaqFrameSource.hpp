#ifndef DUNE_DAQ_PHLEX_DAQFRAMESOURCE_HPP
#define DUNE_DAQ_PHLEX_DAQFRAMESOURCE_HPP

// Phlex 0.3.2 source: read a DUNE DAQ HDF5 file, decode its TPC ADC fragments,
// and emit one Arrow "wc.frame" product (phlex_arrow::TableGroup) per DAQ
// trigger record.  (beads ddm-3j8.1.6)
//
// This is the Phlex read-source half of the DUNE-DAQ read path.  It reuses the
// existing, tested pieces:
//   * dune-daq-hdf     -- DaqHdf5File: navigate the raw HDF5 layout, yield
//                         fragment bytes + identity per trigger record.
//   * dune-daq-codec   -- decode() (fragment bytes -> dense ADC + stream meta)
//                         and OnlineOfflineChannelMap (online -> offline chan).
//   * wire-cell-arrow  -- to_arrow_sparse(): a WCT IFrame -> the four "wc.frame"
//                         Arrow tables (traces/frame_tags/trace_tags/cmm).
// The decoded readout -> WCT IFrame bridge (traces_from/make_frame) is the tiny
// dune-daq-arrow-frame-hdf ToFrame logic; that package exports no CMake target
// (it installs only its tool), so the ~25 lines are reproduced in the .cpp.
//
// A DaqFrameSource is a phlex::source (the official 0.3.x extension point).  It:
//   * OWNS the open DAQ file + the parsed channel map (the shared read state),
//   * create_providers(selector): the implicit-provider FACTORY.  The framework
//     calls it with each downstream input product_selector; ours advertises the
//     uniform phlex_arrow::TableGroup and wires a provider that, for a data
//     cell, decodes that cell's DAQ record into the "wc.frame" tables, and
//   * indices()/cells(): enumerate one "event" data cell per DAQ record, so the
//     colluding file-driven driver (DriverModule.cpp) drives exactly those cells
//     -- the cell count comes from the file, never from config.
//
// The provider ABI (phlex::detail::provider_bundle / product_specification /
// product_ptr) is the one unavoidable "verboten"-named surface; it is spelled
// exactly as Phlex's own FORM source and the sibling phlex-arrow-hdf source do,
// and is confined to this file + phlex-arrow-common's PhlexTypes.hpp aliases.

#include "dune_daq_hdf/DaqHdf5File.hpp"
#include "dune_daq_codec/OnlineOfflineChannelMap.hpp"

#include "phlex/core/product_selector.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/model/index_generator.hpp"
#include "phlex/source.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dune_daq_phlex {

class DaqFrameSource : public phlex::source {
  public:
    /// `input_file`       : the DUNE DAQ HDF5 file to read.
    /// `channel_map_file` : a DUNE ChannelMap*.txt (12- or 13-col, auto-detected)
    ///                      matching the detector of `input_file`.
    /// `tick`             : sample period in WCT seconds (detector configuration;
    ///                      NOT present in the DAQ payload).
    /// `output_creator`   : the creator label the emitted product carries (what
    ///                      downstream nodes route on).
    /// `product`          : the product suffix emitted (e.g. "frame").
    /// `output_layer`     : the Phlex layer the product (and its data cell) lives
    ///                      in (e.g. "event").
    /// `first_record`     : 0-based file-order index of the first trigger record
    ///                      to process (clamped to [0, n_records]).
    /// `max_records`      : maximum number of records to process from
    ///                      `first_record`; <= 0 means all remaining.
    DaqFrameSource(std::string input_file,
                   std::string channel_map_file,
                   double tick,
                   std::string output_creator,
                   std::string product,
                   std::string output_layer,
                   int first_record = 0,
                   int max_records = 0);

    // --- phlex::source interface --------------------------------------------
    phlex::detail::provider_bundles create_providers(
        const phlex::product_selector& selector) override;
    phlex::index_generator indices() override;

    // --- driver-facing view (source/driver collusion) -----------------------
    /// The data cells (one "event" per DAQ trigger record, file order) this
    /// source can supply a frame for.  A file-driven driver yields exactly these.
    const std::vector<phlex::data_cell_index_ptr>& cells() const { return m_cells; }

  private:
    std::shared_ptr<dune_daq_hdf::DaqHdf5File> m_file;  // open DAQ file (const reads)
    std::shared_ptr<dune_daq_codec::OnlineOfflineChannelMap> m_cmap;  // parsed once
    std::shared_ptr<const std::vector<dune_daq_hdf::RecordID>> m_records;  // file order
    double m_tick;
    std::string m_output_creator;
    std::string m_product;
    std::string m_output_layer;

    std::vector<phlex::data_cell_index_ptr> m_cells;  // one per record (number = index)
};

}  // namespace dune_daq_phlex

#endif  // DUNE_DAQ_PHLEX_DAQFRAMESOURCE_HPP
