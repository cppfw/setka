#include <iostream>

#include <setka/init_guard.hpp>

int main(int argc, const char** argv){
    auto guard = setka::get_init_guard_reference();

    std::cout << "Hello setka!" << std::endl;

    return 0;
}
