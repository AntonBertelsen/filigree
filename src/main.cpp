#include "core/Engine.hpp"
#include <iostream>

int main() {
    try {
        Engine engine;
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Engine exited cleanly." << std::endl;
    return EXIT_SUCCESS;
}

