#include <unistd.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include "barf.hpp"
#include "barf-jit.hpp"

// vim: set ts=2 sw=2 tw=0 :

typedef bool (*chain_function)(bool);

bool parallel_chain(bool x) { return true; }
bool series_chain(bool x) { return x; }
bool shuttle_chain(bool x) { return !x; }

std::map<std::string, chain_function> known_chains = {
	{ "parallel", parallel_chain }, { "series", series_chain },
	{ "shuttle", shuttle_chain }
};

class output_wrangler : public barf::check_iterator {
public:
	output_wrangler(std::shared_ptr<llvm::ExecutionEngine> &engine,
									llvm::Module *module,
									std::shared_ptr<barf::ast_node> &node,
									std::string name,
									chain_function c,
									std::shared_ptr<htsFile> o,
									std::shared_ptr<output_wrangler> n)
			: barf::check_iterator::check_iterator(engine, module, node, name),
				chain(c), output_file(o), next(n) {}

	bool wantChromosome(std::shared_ptr<bam_hdr_t> &header, uint32_t tid) {
		return check_iterator::wantChromosome(header, tid) ||
					 next && chain(false) && next->wantChromosome(header, tid);
	}

	void ingestHeader(std::shared_ptr<bam_hdr_t> &header) {
		sam_hdr_write(output_file.get(), header.get());
		if (next)
			next->ingestHeader(header);
	}

	void readMatch(bool matches,
								 std::shared_ptr<bam_hdr_t> &header,
								 std::shared_ptr<bam1_t> &read) {
		if (matches) {
			accept_count++;
			sam_write1(output_file.get(), header.get(), read.get());
		} else {
			reject_count++;
		}
		if (next && chain(matches)) {
			next->processRead(header, read);
		}
	}

	void write_summary() {
		std::cout << "Accepted: " << accept_count << std::endl
							<< "Rejected: " << reject_count << std::endl;
		if (next) {
			next->write_summary();
		}
	}

private:
	chain_function chain;
	std::shared_ptr<htsFile> output_file;
	barf::filter_function filter;
	barf::index_function index;
	std::shared_ptr<output_wrangler> next;
	size_t accept_count = 0;
	size_t reject_count = 0;
};

/**
 * Use LLVM to compile a query, JIT it, and run it over a BAM file.
 */
int main(int argc, char *const *argv) {
	const char *input_filename = nullptr;
	bool binary = false;
	chain_function chain = parallel_chain;
	bool help = false;
	bool ignore_index = false;
	int c;

	while ((c = getopt(argc, argv, "bc:f:hI")) != -1) {
		switch (c) {
		case 'b':
			binary = true;
			break;
		case 'c':
			if (known_chains.find(optarg) == known_chains.end()) {
				std::cerr << "Unknown chaining method: " << optarg << std::endl;
				return 1;
			} else {
				chain = known_chains[optarg];
			}
			break;
		case 'h':
			help = true;
			break;
		case 'I':
			ignore_index = true;
			break;
		case 'f':
			input_filename = optarg;
			break;
		case '?':
			fprintf(stderr, "Option -%c is not valid.\n", optopt);
			return 1;
		default:
			abort();
		}
	}
	if (help) {
		std::cout << argv[0] << " [-b] [-c] [-I] [-v] -f input.bam "
														" query1 output1.bam ..." << std::endl;
		std::cout << "Filter a BAM/SAM file based on the provided query. For "
								 "details, see the man page." << std::endl;
		std::cout << "\t-b\tThe input file is binary (BAM) not text (SAM)."
							<< std::endl;
		std::cout << "\t-c\tChain the queries, rather than use them independently."
							<< std::endl;
		std::cout << "\t-I\tDo not use the index, even if it exists." << std::endl;
		std::cout << "\t-v\tPrint some information along the way." << std::endl;
		return 0;
	}

	if (optind >= argc) {
		std::cout << "Need a query and a BAM file." << std::endl;
		return 1;
	}
	if ((argc - optind) % 2 != 0) {
		std::cout << "Queries and BAM files must be paired." << std::endl;
		return 1;
	}
	if (input_filename == nullptr) {
		std::cout << "An input file is required." << std::endl;
		return 1;
	}
	// Create a new LLVM module and JIT
	LLVMInitializeNativeTarget();
	auto module = new llvm::Module("barf", llvm::getGlobalContext());
	auto engine = barf::createEngine(module);
	if (!engine) {
		return 1;
	}

	std::shared_ptr<output_wrangler> output;
	for (auto it = argc - 2; it >= optind; it -= 2) {
		auto output_file =
				std::shared_ptr<htsFile>(hts_open(argv[it + 1], "wb"), hts_close);
		if (!output_file) {
			perror(optarg);
			return 1;
		}
		// Parse the input query.
		std::shared_ptr<barf::ast_node> ast = barf::ast_node::parse_with_logging(
				std::string(argv[it]), barf::getDefaultPredicates());
		if (!ast) {
			return 1;
		}
		std::stringstream function_name;
		function_name << "filter" << it;

		output = std::make_shared<output_wrangler>(
				engine, module, ast, function_name.str(), chain, output_file, output);
	}

	if (output->processFile(input_filename, binary, ignore_index)) {
		output->write_summary();
	}
	return 0;
}