// Phlex plugin: the file-driven read DRIVER for DUNE DAQ HDF5 input
// (beads ddm-3j8.1.6, the source/driver collusion).
//
// It receives the SAME DaqFrameSource instance registered under the job's
// "sources" section (named in this driver's `uses_sources`), and drives exactly
// the data cells that source discovered by scanning the DAQ file -- one "event"
// per trigger record.  So the cells driven and the cells the source can supply
// a frame for coincide by construction, with no preconfigured cell count.
//
// Phlex requires the layer *shape* (the fixed_hierarchy) at driver-construction
// time, while the source object is only available as the driver function's typed
// argument at run time.  So the job declares the layer name path here (`layers`,
// outermost -> leaf) -- only the layer STRUCTURE, never cell counts -- and the
// source supplies which/how many cells exist.
//
// Config keys (under "driver"):
//   cpp          = "dune_daq_phlex_driver"
//   uses_sources (list, required): the source name(s) to drive; exactly one
//                DaqFrameSource is expected here.
//   layers       (list<string>, required): the data-cell layer path from the
//                job root to the leaf, e.g. ["event"].  Must match the source's
//                output_layer (the layer its cells and product live in).
//
// Example:
//   driver: {
//     cpp: 'dune_daq_phlex_driver',
//     uses_sources: ['daq'],
//     layers: ['event'],
//   }

#include "dune_daq_phlex/DaqFrameSource.hpp"

#include "phlex/configuration.hpp"
#include "phlex/driver.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/model/fixed_hierarchy.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

PHLEX_REGISTER_DRIVER(d, config)
{
    auto const layers = config.get<std::vector<std::string>>("layers");

    // One layer path (job root -> leaf); fixed_hierarchy adds every cumulative
    // prefix (and "job") to the set of layers cells are validated against.
    phlex::fixed_hierarchy hierarchy{std::vector<std::vector<std::string>>{layers}};

    return d.driver(
      std::move(hierarchy),
      [](phlex::data_cell_yielder const yield, dune_daq_phlex::DaqFrameSource const& src) {
          // One-line startup diagnostic on stderr (the plugin's spdlog default
          // logger is not wired to phlex's configured sink across the DSO
          // boundary): reports how many cells were discovered and driven.
          std::cerr << "dune_daq_phlex_driver: driving " << src.cells().size()
                    << " trigger record(s)\n";
          yield(phlex::data_cell_index::job());
          for (auto const& cell : src.cells()) {
              yield(cell);
          }
      });
}
