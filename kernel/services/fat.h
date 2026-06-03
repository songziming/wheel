#ifndef FAT_H
#define FAT_H

typedef struct volumn volumn_t;

typedef struct volumn_ops {
    //
} volumn_ops_t;



struct volumn {
    const volumn_ops_t *ops;
};

#endif // FAT_H
