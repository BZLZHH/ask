#include <exception>
#include <iostream>

#include "ask/cli.hpp"

int main(int argc, char** argv) {
  try {
    return ask::run_cli(ask::parse_cli(argc, argv));
  } catch (const std::exception& error) {
    std::cerr << "ask: " << error.what() << '\n';
    return 1;
  }
}
