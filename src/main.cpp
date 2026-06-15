#include "Application.hpp"
#include <iostream>

int main() {
    Application app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Application exited cleanly." << std::endl;
    return EXIT_SUCCESS;
}
