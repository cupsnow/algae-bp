#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif // cplusplus

typedef enum {
    log_severity_error = 1,
    log_severity_info,
    log_severity_debug,
    log_severity_verbose,
} log_severity_t;

#define log_serverity_str(_lvl, _def) \
        (((_lvl) == log_severity_error) ? "ERROR" : \
        ((_lvl) == log_severity_info) ? "INFO" : \
        ((_lvl) == log_severity_debug) ? "Debug" : \
        ((_lvl) == log_severity_verbose) ? "verbose" : \
        (_def))

#define log_m(_lvl, _fmt, _args...) printf("[%s][%s][#%d]" _fmt, log_serverity_str(_lvl, ""), __func__, __LINE__, ##_args)
#define log_d(_args...) log_m(log_severity_debug, _args)
#define log_e(_args...) log_m(log_severity_error, _args)

#ifdef __cplusplus
} // extern "C" {
#endif // cplusplus

#ifdef __cplusplus

#include <string>

namespace cm01 {

std::string string_format(const std::string &format, ...);

} // cm01

#endif // cplusplus

#endif // UTILS_H
