#include <ludus/foundation/base/version.hpp>

#include <iostream>

int main()
{
    std::cout << ludus::foundation::version_string() << std::endl;

    return 0;
}