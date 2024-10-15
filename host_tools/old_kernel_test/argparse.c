#include "argparse.h"

#include <stdlib.h>
#include <string.h>

// 解析命令行参数
// 参数以`-`或`--`开头, 未识别的参数留在argv中, 返回剩余参数个数
// 相当于解析之后, argc / argv只剩下exe名称和未识别参数, 可以连续多次调用
// @param argc  输入参数个数
// @param argv  输入参数数组, 同时也用于输出未识别的参数
// @param optc  参数定义数组长度
// @param opts  参数定义数组
// @retval      未识别的参数个数, 也就是输出`argv`的长度
int parse_args(int argc, char* argv[], size_t optc, opt_def_t* opts) {
    if (0 == optc) {
        return argc;
    }
    int rest_cnt = 1;
    for (int i = 1; i < argc; ++i) {
        char* curr_arg = argv[i];
        opt_def_t* opt = NULL;
        if ('-' == curr_arg[0]) {
            if ('-' == curr_arg[1]) {
                for (size_t j = 0; j < optc; ++j) {
                    if (NULL == opts[j].name) {
                        continue;
                    }
                    if (0 == strcmp(opts[j].name, &curr_arg[2])) {
                        opt = &opts[j];
                        break;
                    }
                }
            }
            else if ((0 != curr_arg[1]) && (0 == curr_arg[2])) {
                for (size_t j = 0; j < optc; ++j) {
                    if (0 == opts[j].abbr)
                        continue;
                    if (opts[j].abbr == curr_arg[1]) {
                        opt = &opts[j];
                        break;
                    }
                }
            }
        }
        if (NULL == opt) {
            argv[rest_cnt++] = curr_arg;
            continue;
        }
        switch (opt->type) {
        case OPT_BINARY: *(char*)opt->data = 1;              break;
        case OPT_STRING: *(char**)opt->data = argv[++i];     break;
        case OPT_NUMBER: *(int*)opt->data = atoi(argv[++i]); break;
        default: break;
        }
    }

    return rest_cnt;
}

int args_maxlen(size_t optc, opt_def_t* opts, int maxlen) {
    for (size_t i = 0; i < optc; ++i) {
        int len = (int)strlen(opts[i].name);
        if (len > maxlen) {
            maxlen = len;
        }
    }
    return maxlen;
}

// 打印帮助信息, 通常在用户指定`-h`后执行
// @param fd    输出文件流, 用户可以选择`stdout`还是`stderr`
// @param optc  参数定义数组长度
// @param opts  参数定义数组
void print_args(FILE* fd, size_t optc, opt_def_t* opts, int maxlen) {
    for (size_t i = 0; i < optc; ++i) {
        if (0 != opts[i].abbr) {
            fprintf(fd, "   -%c | ", opts[i].abbr);
        }
        else {
            fprintf(fd, "           ");
        }
        fprintf(fd, "--%-*s ", maxlen, opts[i].name);
        switch (opts[i].type) {
        case OPT_NUMBER: fprintf(fd, "<n>   "); break;
        case OPT_STRING: fprintf(fd, "<s>   "); break;
        case OPT_BINARY: fprintf(fd, "      "); break;
        default:                                break;
        }
        fprintf(fd, "%s\n", opts[i].help);
    }
}
