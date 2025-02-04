#include <iostream>
#include <set>

#include <sharg/detail/to_string.hpp>
#include <sharg/exceptions.hpp>

#include <chopper/configuration.hpp>
#include <chopper/layout/aggregate_by.hpp>
#include <chopper/layout/hierarchical_binning.hpp>
#include <chopper/layout/ibf_query_cost.hpp>
#include <chopper/layout/output.hpp>
#include <chopper/layout/previous_level.hpp>

namespace chopper::layout
{

size_t determine_best_number_of_technical_bins(chopper::data_store & data, chopper::configuration & config)
{
    std::stringstream * const output_buffer_original = data.output_buffer;
    std::stringstream * const header_buffer_original = data.header_buffer;

    std::set<size_t> potential_t_max = [&]()
    {
        std::set<size_t> result;

        for (size_t t_max = 64; t_max <= config.tmax; t_max *= 2)
            result.insert(t_max);

        // Additionally, add the t_max that is closest to the sqrt() of the number of
        // user bins, as it is expected to evenly spread bins and may perform well.
        size_t const user_bin_count{std::ranges::size(data.kmer_counts)};
        size_t const sqrt_t_max{next_multiple_of_64(std::ceil(std::sqrt(user_bin_count)))};
        result.insert(sqrt_t_max);

        return result;
    }();

    // with -determine-best-tmax the algorithm is executed multiple times and result with the minimum
    // expected query costs are written to the standard output

    std::ofstream file_out{config.output_filename.string() + ".stats"};

    file_out << "## ### Parameters ###\n"
              << "## number of user bins = " << data.filenames.size() << '\n'
              << "## number of hash functions = " << config.num_hash_functions << '\n'
              << "## false positive rate = " << config.false_positive_rate << '\n';
    hibf_statistics::print_header_to(file_out, config.output_verbose_statistics);

    double best_expected_HIBF_query_cost{std::numeric_limits<double>::infinity()};
    size_t best_t_max{};
    size_t max_hibf_id{};
    size_t t_max_64_memory{};

    for (size_t const t_max : potential_t_max)
    {
        std::stringstream output_buffer_tmp;
        std::stringstream header_buffer_tmp;
        config.tmax = t_max;                               // overwrite tmax
        data.output_buffer = &output_buffer_tmp;           // overwrite buffer
        data.header_buffer = &header_buffer_tmp;           // overwrite buffer
        data.previous = chopper::layout::previous_level{}; // reset previous IBF, s.t. data refers to top level IBF

        chopper::layout::hibf_statistics global_stats{config, data.fp_correction, data.kmer_counts};
        data.stats = &global_stats.top_level_ibf;

        // execute the actual algorithm
        size_t const max_hibf_id_tmp = chopper::layout::hierarchical_binning{data, config}.execute();

        global_stats.finalize();

        global_stats.print_summary_to(t_max_64_memory, file_out, config.output_verbose_statistics);

        // Use result if better than previous one.
        if (global_stats.expected_HIBF_query_cost < best_expected_HIBF_query_cost)
        {
            *output_buffer_original = std::move(output_buffer_tmp);
            *header_buffer_original = std::move(header_buffer_tmp);
            max_hibf_id = max_hibf_id_tmp;
            best_t_max = t_max;
            best_expected_HIBF_query_cost = global_stats.expected_HIBF_query_cost;
        }
        else if (!config.force_all_binnings)
        {
            break;
        }
    }

    file_out << "# Best t_max (regarding expected query runtime): " << best_t_max << '\n';
    config.tmax = best_t_max;
    data.output_buffer = output_buffer_original; // reset data buffers
    data.header_buffer = header_buffer_original; // reset data buffers
    return max_hibf_id;
}

int execute(chopper::configuration & config, chopper::data_store & data)
{
    if (config.rearrange_user_bins)
        config.estimate_union = true;

    if (config.tmax % 64 != 0)
    {
        config.tmax = chopper::next_multiple_of_64(config.tmax);
        std::cerr << "[CHOPPER LAYOUT WARNING]: Your requested number of technical bins was not a multiple of 64. "
                  << "Due to the architecture of the HIBF, it will use up space equal to the next multiple of 64 "
                  << "anyway, so we increased your number of technical bins to " << config.tmax << ".\n";
    }

    data.compute_fp_correction(config.false_positive_rate, config.num_hash_functions, config.tmax);

    // TODO aggregating is outdated. Has to be reworked when needed
    // If requested, aggregate the data before layouting them
    // if (config.aggregate_by_column != -1)
    //     aggregate_by(data, config.aggregate_by_column - 2/*user index includes first two columns (filename, count)*/);

    size_t max_hibf_id;

    if (config.determine_best_tmax)
    {
        max_hibf_id = determine_best_number_of_technical_bins(data, config);
    }
    else
    {
        chopper::layout::hibf_statistics global_stats{config, data.fp_correction, data.kmer_counts};
        data.stats = &global_stats.top_level_ibf;
        size_t dummy{};

        max_hibf_id = chopper::layout::hierarchical_binning{data, config}.execute(); // just execute once

        if (config.output_verbose_statistics)
        {
            global_stats.print_header_to(std::cout);
            global_stats.print_summary_to(dummy, std::cout);
        }
    }

    // brief Write the output to the layout file.
    std::ofstream fout{config.output_filename};
    write_layout_header_to(config, max_hibf_id, data.header_buffer->str(), fout);
    fout << data.output_buffer->str();

    return 0;
}

} // namespace chopper::layout
