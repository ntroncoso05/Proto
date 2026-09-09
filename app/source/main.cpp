#include <engine.hpp>
#include <iostream>
#include <fstream>
#include <vector>


#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
    Core::Engine app;
    try {
        app.Initialize();
        app.Run();
        app.CleanUp();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}