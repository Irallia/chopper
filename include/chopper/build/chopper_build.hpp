#pragma once

#define SEQAN_HAS_ZLIB 1
#define SEQAN3_HAS_ZLIB 1

#include <fstream>

#include <cereal/archives/binary.hpp>

#include <seqan3/argument_parser/all.hpp>
#include <seqan3/core/debug_stream.hpp>

#include <chopper/build/build_config.hpp>
#include <chopper/build/create_ibfs_from_chopper_pack.hpp>
#include <chopper/build/create_ibfs_from_chopper_split.hpp>

void initialize_argument_parser(seqan3::argument_parser & parser, build_config & config)
{
    parser.info.author = "Avenja";
    parser.info.short_description = "Build IBF on results from chopper-split.";
    parser.info.version = "1.0.0";

    parser.add_option(config.chopper_pack_filename, 'p', "pack-file", "Provide the file produced by chopper pack.");
    // parser.add_option(config.chopper_split_filename, 's', "split-file", "Provide the file produced by chopper split.");
    parser.add_option(config.k, 'k', "kmer-size", "The kmer size to build kmers.");
    parser.add_option(config.overlap, 'l', "overlap", "The overlap between split regions of the same sequence.");
    parser.add_option(config.FPR, 'r', "false-positive-rate", "The minimum false positive rate of every IBF.");
    parser.add_option(config.output_prefix, 'o', "out-prefix", "Prefix of the output files.");
    parser.add_flag(config.verbose, 'v', "verbose", "Output logging/progress information.");
}

auto create_ibfs(seqan3::argument_parser const & parser, build_data const & data, build_config const & config)
{
    // assert(parser.is_option_set('p') || parser.is_option_set('s'));

    // if (parser.is_option_set('p'))
        create_ibfs_from_chopper_pack(data, config);
    // else
    //     create_ibfs_from_chopper_split(data, config);
}

int chopper_build(seqan3::argument_parser & parser)
{
    build_config config{};
    initialize_argument_parser(parser, config);

    try
    {
        parser.parse();

        if (parser.is_option_set('p') && parser.is_option_set('s'))
        {
            seqan3::debug_stream << "[CHOPPER BIULD ERROR] Options -p/--pack-file and -s/--split_file "
                                 << "are mututal exclusive.\n";
            return -1;
        }
        else if (!parser.is_option_set('p') && !parser.is_option_set('s'))
        {
            seqan3::debug_stream << "[CHOPPER BIULD ERROR] Either option -p/--pack-file or -s/--split_file "
                                 << "must be provided.\n";
            return -1;
        }
    }
    catch (seqan3::argument_parser_error const & ext)
    {
        seqan3::debug_stream << "[CHOPPER BIULD ERROR] " << ext.what() << "\n";
        return -1;
    }

    build_data data{};
    create_ibfs(parser, data, config);

    // Create output directory if it does not exist
    std::filesystem::path directory = std::filesystem::path{config.output_prefix}.parent_path();
    if (!std::filesystem::exists(directory))
        if (!std::filesystem::create_directories(directory)) // recursively creates every directory needed
            throw std::runtime_error{"Could not create directory " + directory.string()};

    // write vector of ibfs to file
    {
        std::string const out_filename{config.output_prefix + "hibf.out"}; // the complete hierarchical ibf
        std::ofstream fout(out_filename, std::ios::binary);

        if (!fout.good() || !fout.is_open())
            throw std::runtime_error{"Could not open " + out_filename + " for writing."};

        cereal::BinaryOutputArchive archive(fout);
        archive(data.ibfs);
    }

    // write ibf mapping to file
    {
        std::string const out_filename{config.output_prefix + "position_mapping.out"};
        std::ofstream fout(out_filename, std::ios::binary);

        if (!fout.good() || !fout.is_open())
            throw std::runtime_error{"Could not open " + out_filename + " for writing."};

        cereal::BinaryOutputArchive archive(fout);
        archive(data.ibf_mapping);
    }

    return 0;
}

