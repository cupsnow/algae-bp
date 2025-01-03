#include <iostream>
#include "utils.h"

int main(int argc, char* argv[]) {

    // Your application logic here
    for (int i = 0; i < argc; ++i) {
        log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
        printf("%s\n", cm01::string_format("argv[%d/%d]: %s", i + 1, argc, 
                argv[i]).c_str());
    }

    return 0;
}
