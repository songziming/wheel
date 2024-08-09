#ifndef ARGPARSE_H
#define ARGPARSE_H

#include <stdio.h>

typedef enum opt_type {
    OPT_BINARY, ///<布尔类型,无参数
    OPT_STRING, ///<跟字符串类型参数
    OPT_NUMBER, ///<跟整数类型参数
} opt_type_t;


typedef struct opt_def {
    const char* name;   ///<选项全称,"--xxx'(此处不含`--`)
    const char  abbr;   /// 单字母缩写,'-x'
    opt_type_t  type;   ///<选项类型
    void* data;         ///<后跟参数的取值
    const char* help;   ///<帮助信息
} opt_def_t;

int parse_args(int argc, char* argv[], size_t optc, opt_def_t* opts);
int args_maxlen(size_t optc, opt_def_t* opts, int maxlen);
void print_args(FILE* fd, size_t optc, opt_def_t* opts, int maxlen);

#endif // ARGPARSE_H
