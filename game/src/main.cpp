#include "core/app.h"

int main(int argc, char** argv) {
    opente::core::App app;
    if (!app.init(argc > 0 ? argv[0] : "")) {
        return 1;
    }
    return app.run();
}
