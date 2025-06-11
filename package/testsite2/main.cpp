#include <stdio.h>

#define log_m(_lv, _fmt, _args...) printf(_lv "[%s][#%d]" _fmt, __func__, __LINE__, ##_args)
#define log_d(...) log_m("[Debug]", ##__VA_ARGS__)

int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
    }
        
    return 0;
}
