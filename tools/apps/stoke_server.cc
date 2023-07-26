#include <chrono>
#include <iostream>
#include <sys/time.h>


#include "src/ext/cpputil/include/command_line/command_line.h"
#include "src/ext/cpputil/include/io/column.h"
#include "src/ext/cpputil/include/io/console.h"
#include "src/ext/cpputil/include/io/filterstream.h"
#include "src/ext/cpputil/include/signal/debug_handler.h"

#include "src/cfg/cfg_transforms.h"
#include "src/expr/expr.h"
#include "src/expr/expr_parser.h"
#include "src/tunit/tunit.h"
#include "src/search/progress_callback.h"
#include "src/search/new_best_correct_callback.h"
#include "src/search/statistics_callback.h"
#include "src/search/failed_verification_action.h"
#include "src/search/postprocessing.h"
// #include "src/cereal-1.3.2/"
// #include "src/cereal/types/unordered_map.hpp"
// #include "src/cereal/types/memory.hpp"
// #include "src/cereal/archives/binary.hpp"

#include "tools/args/search.inc"
#include "tools/args/target.inc"
#include "tools/gadgets/cost_function.h"
#include "tools/gadgets/correctness_cost.h"
#include "tools/gadgets/functions.h"
#include "tools/gadgets/sandbox.h"
#include "tools/gadgets/search.h"
#include "tools/gadgets/search_state.h"
#include "tools/gadgets/seed.h"
#include "tools/gadgets/solver.h"
#include "tools/gadgets/target.h"
#include "tools/gadgets/testcases.h"
#include "tools/gadgets/transform_pools.h"
#include "tools/gadgets/validator.h"
#include "tools/gadgets/verifier.h"
#include "tools/gadgets/weighted_transform.h"
#include "tools/io/postprocessing.h"
#include "tools/io/failed_verification_action.h"
#include "src/search/search.h"
#include "src/transform/weighted.h"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/asio.hpp>
#include "search.h"

namespace fs = boost::filesystem;
using namespace boost::asio;
using ip::tcp;

using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace chrono;

auto code_to_string (x64asm::Code code) {
    stringstream ss;
    ss << code;
    auto res = regex_replace(ss.str(), regex("\n"), "\\n");
    return res;
};

// string state_to_string (stoke::output_iterator io_pairs) {
//     stringstream ss;
//     ss << io_pairs;
//     auto res = regex_replace(ss.str(), regex("\n"), "\\n"))
//     return res;
// };

auto& out = ValueArg<string>::create("out")
            .alternate("o")
            .usage("<path/to/file.s>")
            .description("File to write successful results to")
            .default_val("result.s");

auto& results_arg = ValueArg<string>::create("results")
                    .usage("<path/to/dir>")
                    .description("Path to the directory where new best correct rewrites are being stored.  Rewrites are verified before being stored (using the same verification settings as the final verification).")
                    .default_val("");

auto& machine_output_arg = ValueArg<string>::create("machine_output")
                           .usage("<path/to/file.s>")
                           .description("Machine-readable output (result and statistics)");

auto& postprocessing_arg =
  ValueArg<Postprocessing, PostprocessingReader, PostprocessingWriter>::create("postprocessing")
  .usage("(none|simple|full)")
  .description("Postprocessing of the program found by STOKE (simple removes nops and unreachable blocks, and full additionally removes redundant statements without side-effects)")
  .default_val(Postprocessing::FULL);

void new_best_correct_callback(const NewBestCorrectCallbackData& data, void* arg) {

  if (results_arg.has_been_provided()) {
    Console::msg() << "Verifying improved rewrite..." << endl;

    auto state = data.state;
    auto data = (pair<VerifierGadget&, TargetGadget&>*)arg;
    auto verifier = data->first;
    auto target = data->second;

    // perform the postprocessing
    Cfg res(state.current);
    if (postprocessing_arg == Postprocessing::FULL) {
      CfgTransforms::remove_redundant(res);
      CfgTransforms::remove_unreachable(res);
      CfgTransforms::remove_nop(res);
    } else if (postprocessing_arg == Postprocessing::SIMPLE) {
      CfgTransforms::remove_unreachable(res);
      CfgTransforms::remove_nop(res);
    } else {
      // Do nothing.
    }

    // verify the new best correct rewrite
    const auto verified = verifier.verify(target, res);

    if (verifier.has_error()) {
      Console::msg() << "The verifier encountered an error: " << verifier.error() << endl << endl;
    }

    // save to file if verified
    if (verified) {
      Console::msg() << "Verified!  Saving result..." << endl << endl;
      // next name for result file
      string name = "";
      bool done = false;
      do {
        name = results_arg.value() + "/result-" + to_string(state.last_result_id) + ".s";
        state.last_result_id += 1;
        ifstream f(name.c_str());
        done = !f.good();
      } while (!done);

      // write output
      ofstream outfile;
      outfile.open(name);
      outfile << res.get_function();
      outfile.close();
    } else {
      Console::msg() << "Verification failed."  << endl << endl;
      if (verifier.counter_examples_available()) {
        Console::msg() << "Counterexample: " << endl;
        for (auto it : verifier.get_counter_examples()) {
          Console::msg() << it << endl;
        }
      }
    }

  } else {
    cout << "No action on new best correct" << endl;

  }

}

static Cost lowest_cost = 0;
static Cost lowest_correct = 0;
static Cost starting_cost = 0;


int main(int argc, char **argv)
{
    // Create the I/O service
    boost::asio::io_service io_service;

    // Create an acceptor to listen for incoming connections
    tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), 8080));

    CommandLineConfig::strict_with_convenience(argc, argv);
    SeedGadget seed;
    FunctionsGadget aux_fxns;
    TargetGadget target(aux_fxns, init_arg == Init::ZERO);

    TrainingSetGadget training_set(seed);
    SandboxGadget training_sb(training_set, aux_fxns);

    TransformPoolsGadget transform_pools(target, aux_fxns, seed);
    WeightedTransformGadget transform(transform_pools, seed);
    SearchGadget search(&transform, seed);

    TestSetGadget test_set(seed);
    SandboxGadget test_sb(test_set, aux_fxns);

    PerformanceSetGadget perf_set(seed);
    SandboxGadget perf_sb(perf_set, aux_fxns);

    CorrectnessCostGadget holdout_fxn(target, &test_sb);
    VerifierGadget verifier(test_sb, holdout_fxn);

    auto nbcc_data = pair<VerifierGadget&, TargetGadget&>(verifier, target);
    search.set_new_best_correct_callback(new_best_correct_callback, &nbcc_data);

    size_t total_iterations = 0;
    size_t total_restarts = 0;

    SearchStateGadget state(target, aux_fxns);
    CostFunctionGadget fxn(target, &training_sb, &perf_sb);
    auto initial_cost = fxn(state.current);
    if (!initial_cost.first && init_arg == Init::TARGET) {
        Console::warn() << "Initial state has non-zero correctness cost with --init target.";
    }
    starting_cost = initial_cost.second;
    lowest_cost = initial_cost.second;
    if (initial_cost.first) {
        lowest_correct = initial_cost.second;
    } else {
        lowest_correct = 0;
    }
    search.configure(target, fxn, state, aux_fxns);
    assert(state.best_yet.is_sound());
    assert(state.best_correct.is_sound());
    search.move_statistics = vector<Statistics>(static_cast<WeightedTransform*>(search.transform_)->size());
    TransformInfo ti;

    while (true)
    {
        // Wait for a client to connect
        tcp::socket socket(io_service);
        acceptor.accept(socket);

        // Read data from the client
        boost::asio::streambuf buffer;
        boost::system::error_code error;
        size_t len = boost::asio::read_until(socket, buffer, '\n', error);

        if (!error)
        {
            // Process the request
            std::istream input(&buffer);
            std::string message;
            std::getline(input, message);
            int action = stoi(message);
            std::cout << "Received message from client: " << message << std::endl;
            
            ti = (*search.transform_).act(state.current, action);
            std::string code = code_to_string(state.current.get_code());


            
            // std::string cpu_state = state_to_string(perf_sb.get_output(perf_sb.num_inputs()));
            // auto output_it = perf_sb.get_output(perf_sb.num_inputs());
            // for (auto it = output_it->begin(); it != output_it->end(); ++it) {
            // auto it = perf_sb.get_output(1);
            // auto data = next(it);
            
            // }
            // std::string code = code_to_string(state.best_yet.get_code());
            
            // search.act(target, fxn, init_arg, state, aux_fxns, action);
            // Send response back to the client
            std::string response = "Server says: " + message + "\n";
            boost::asio::write(socket, boost::asio::buffer(code));
            // boost::asio::write(socket, boost::asio::buffer(code));
        }
        else
        {
            std::cerr << "Error reading from socket: " << error.message() << std::endl;
        }
    }

    return 0;
}