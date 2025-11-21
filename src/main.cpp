#include "Renderer.h"
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <shader_path>\n";
        return 1;
    }
    Renderer renderer(argv[1]);
    renderer.init();
    renderer.run();
    renderer.destroy();
    return 0;
}
