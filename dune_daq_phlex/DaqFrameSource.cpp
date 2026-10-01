#include "dune_daq_phlex/DaqFrameSource.hpp"

#include "phlex_arrow_common/PhlexSource.hpp"  // provide_if_selected (experimental-API choke point)
#include "phlex_arrow_common/TableGroup.hpp"   // phlex_arrow::TableGroup ("wc.frame")

#include "dune_daq_codec/Decode.hpp"
#include "dune_daq_types/FragmentType.hpp"

#include "wire_cell_arrow/Converters.hpp"      // WireCell::Arrow::to_arrow_sparse
#include "WireCellAux/SimpleFrame.h"
#include "WireCellAux/SimpleTrace.h"
#include "WireCellIface/IFrame.h"
#include "WireCellIface/ITrace.h"

#include "phlex/concurrency.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace dune_daq_phlex {

namespace {

// TPC ADC fragment types the codec decodes (mirrors daq_hdf_to_arrow).
bool is_tpc_adc(dune_daq::FragmentType t)
{
    return t == dune_daq::FragmentType::kWIBEth || t == dune_daq::FragmentType::kTDEEth;
}

// Decoded readout -> WCT ITraces with OFFLINE channel ids (dune-daq-arrow-frame-hdf
// ToFrame logic, reproduced: that package exports no linkable CMake target).
WireCell::ITrace::vector traces_from(const dune_daq_codec::DenseAdc& adc,
                                     const dune_daq_codec::StreamMeta& meta,
                                     const dune_daq_codec::ChannelMap& channel_map)
{
    WireCell::ITrace::vector traces;
    traces.reserve(adc.n_channels);
    for (unsigned c = 0; c < adc.n_channels; ++c) {
        const int chid = channel_map.offline(meta.det_id, meta.crate_id, meta.slot_id,
                                              meta.stream_id, c);
        WireCell::ITrace::ChargeSequence charge(adc.n_ticks);
        for (std::size_t t = 0; t < adc.n_ticks; ++t) {
            charge[t] = static_cast<float>(adc.at(c, t));
        }
        traces.push_back(std::make_shared<WireCell::Aux::SimpleTrace>(chid, 0, charge));
    }
    return traces;
}

WireCell::IFrame::pointer make_frame(int ident, double time, double tick,
                                     const WireCell::ITrace::vector& traces)
{
    return std::make_shared<WireCell::Aux::SimpleFrame>(ident, time, traces, tick);
}

// Read + decode one DAQ trigger record into the four "wc.frame" Arrow tables.
phlex_arrow::TableGroup frame_table_group(const dune_daq_hdf::DaqHdf5File& file,
                                          const dune_daq_codec::ChannelMap& cmap,
                                          const dune_daq_hdf::RecordID& rid,
                                          double tick)
{
    WireCell::ITrace::vector traces;
    for (const auto& fi : file.fragments(rid)) {
        if (!is_tpc_adc(fi.type)) continue;
        auto bytes = file.read_bytes(fi.dataset_path);
        dune_daq_codec::DecodedFragment dec;
        try {
            dec = dune_daq_codec::decode(std::span<const std::byte>(bytes));
        }
        catch (const std::exception&) {
            continue;  // undecodable fragment: skip (matches the reference tool)
        }
        auto tr = traces_from(dec.adc, dec.meta, cmap);
        traces.insert(traces.end(), tr.begin(), tr.end());
    }

    // Ascending OFFLINE channel-id order (planes become contiguous ranges), the
    // row order offline consumers expect.  Unmapped channels (id<0) sort first.
    std::stable_sort(traces.begin(), traces.end(),
                     [](const WireCell::ITrace::pointer& a, const WireCell::ITrace::pointer& b) {
                         return a->channel() < b->channel();
                     });

    auto frame = make_frame(static_cast<int>(rid.number), 0.0, tick, traces);
    auto fr = WireCell::Arrow::to_arrow_sparse(frame);
    if (!fr.ok()) {
        throw std::runtime_error("dune_daq_phlex: to_arrow_sparse for " + rid.group + ": "
                                 + fr.status().ToString());
    }
    return phlex_arrow::TableGroup{"wc.frame",
                                   {{"traces", fr->traces},
                                    {"frame_tags", fr->frame_tags},
                                    {"trace_tags", fr->trace_tags},
                                    {"cmm", fr->cmm}}};
}

}  // namespace

DaqFrameSource::DaqFrameSource(std::string input_file,
                               std::string channel_map_file,
                               double tick,
                               std::string output_creator,
                               std::string product,
                               std::string output_layer,
                               int first_record,
                               int max_records,
                               std::string stage)
  : m_file(std::make_shared<dune_daq_hdf::DaqHdf5File>(input_file))
  , m_cmap(std::make_shared<dune_daq_codec::OnlineOfflineChannelMap>(channel_map_file))
  , m_records(std::make_shared<const std::vector<dune_daq_hdf::RecordID>>(m_file->records()))
  , m_tick(tick)
  , m_output_creator(std::move(output_creator))
  , m_product(std::move(product))
  , m_output_layer(std::move(output_layer))
  , m_stage(std::move(stage))
{
    if (m_stage.empty() || m_stage == "CURRENT") {
        throw std::invalid_argument(
          "dune_daq_phlex source: 'stage' must name the data's stage "
          "(not empty, not the reserved \"CURRENT\")");
    }
    // Select the [start, start+count) slice of records to process.  The provider
    // maps a cell back to its record by data_cell_index::number(), so the cell
    // number stays the ORIGINAL file-order index (frame idents keep tracking the
    // real record) even when only a slice is driven.
    const std::size_t n = m_records->size();
    const std::size_t start = std::min(static_cast<std::size_t>(std::max(first_record, 0)), n);
    const std::size_t count = (max_records <= 0)
                                ? (n - start)
                                : std::min(static_cast<std::size_t>(max_records), n - start);

    auto job = phlex::data_cell_index::job();
    m_cells.reserve(count);
    for (std::size_t i = start; i < start + count; ++i) {
        m_cells.push_back(job->make_child(m_output_layer, i));
    }

    if (start != 0 || count != n) {
        std::cerr << "dune_daq_phlex: " << n << " record(s) in file; processing "
                  << count << " (records " << start << ".." << (start + count) << ")\n";
    }
}

phlex::provider_bundles DaqFrameSource::create_providers(
    const phlex::product_selector& selector)
{
    // The one product this source advertises: the uniform "wc.frame" TableGroup,
    // stamped with the configured output creator/suffix/layer and the data stage.
    //
    // libhdf5 is not thread-safe on one handle and decode is stateful per read,
    // so the provider is serial.  It captures the shared read state (open file,
    // channel map, record list) and the tick.
    auto file = m_file;
    auto cmap = m_cmap;
    auto records = m_records;
    auto tick = m_tick;
    return phlex_arrow::provide_if_selected<phlex_arrow::TableGroup>(
      selector, m_output_creator, m_product, m_output_layer, m_stage,
      phlex::concurrency::serial,
      [file, cmap, records, tick](const phlex::data_cell_index& id) {
          const std::size_t ri = id.number();
          if (ri >= records->size()) {
              throw std::runtime_error("dune_daq_phlex: data cell number "
                                       + std::to_string(ri) + " out of range");
          }
          return frame_table_group(*file, *cmap, (*records)[ri], tick);
      });
}

phlex::index_generator DaqFrameSource::indices()
{
    for (auto const& idx : m_cells) {
        co_yield idx;
    }
}

}  // namespace dune_daq_phlex
