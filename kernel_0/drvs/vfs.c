#include <wheel.h>

// VFS is the manager of different kinds of file systems

// folder operations:
// - create
// - delete
// - opendir
// - closedir
// - readdir
// - renamedir
// - link
// - unlink

//------------------------------------------------------------------------------
// (sub) file system, mounted to VFS

typedef struct fs_ops fs_ops_t;
typedef struct fs     fs_t;

typedef void (* fs_resolve_t) (fs_t * fs, const char * name);
typedef void (* fs_walk_t)    (fs_t * fs);
typedef void (* fs_stat_t)    (fs_t * fs);

struct fs_ops {
    fs_resolve_t resolve;
    fs_walk_t    walk;
};

struct fs {
    kref_t   ref;
    dlnode_t dl;
    char     name[64];
    fs_ops_t ops;
};

static dllist_t mount_points = DLLIST_INIT;

void vfs_mount(fs_t * fs) {
    fs = kref_retain(fs);
    dl_push_tail(&mount_points, &fs->dl);
}

void vfs_unmount(fs_t * fs) {
    dl_remove(&mount_points, &fs->dl);
    kref_delete(fs);
}
