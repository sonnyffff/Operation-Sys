/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid, Angela Demke Brown
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2022 Angela Demke Brown
 */

/**
 * CSC369 Assignment 4 - vsfs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "bitmap.h"
#include "fs_ctx.h"
#include "map.h"
#include "options.h"
#include "util.h"
#include "vsfs.h"
#define MYPRINTF(a) //printf a
// NOTE: All path arguments are absolute paths within the vsfs file system and
// start with a '/' that corresponds to the vsfs root directory.
//
// For example, if vsfs is mounted at "/tmp/my_userid", the path to a
// file at "/tmp/my_userid/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "/tmp/my_userid/dir/" will be passed to
// FUSE callbacks as "/dir".

/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool
vsfs_init(fs_ctx* fs, vsfs_opts* opts)
{
  size_t size;
  void* image;

  // Nothing to initialize if only printing help
  if (opts->help) {
    return true;
  }

  // Map the disk image file into memory
  image = map_file(opts->img_path, VSFS_BLOCK_SIZE, &size);
  if (image == NULL) {
    return false;
  }

  return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in vsfs_init().
 */
static void
vsfs_destroy(void* ctx)
{
  fs_ctx* fs = (fs_ctx*)ctx;
  if (fs->image) {
    munmap(fs->image, fs->size);
    fs_ctx_destroy(fs);
  }
}

/** Get file system context. */
static fs_ctx*
get_fs(void)
{
  return (fs_ctx*)fuse_get_context()->private_data;
}

/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
static int
path_lookup(const char* path, vsfs_ino_t* ino)
{
  if (path[0] != '/') {
    fprintf(stderr, "Not an absolute path\n");
    return -ENOSYS;
  }
  // TODO: complete this function and any helper functions
  if (strcmp(path, "/") == 0) {
    *ino = VSFS_ROOT_INO;
    return 0;
  }
  /*find corresponding file in root */
  fs_ctx* fs = get_fs();
  vsfs_inode *root_inode = &fs->itable[VSFS_ROOT_INO];
#if 0
  /* testing */
  MYPRINTF(("data_region:%d\n",fs->sb->data_region));
  MYPRINTF(("i_mode:%x\n",root_inode->i_mode));
  MYPRINTF(("i_nlink:%d\n",root_inode->i_nlink));
  MYPRINTF(("i_size:%d\n",(int)root_inode->i_size));
  MYPRINTF(("i_blocks:%d\n",root_inode->i_blocks));
  for(int i = 0;i < VSFS_NUM_DIRECT;i++) {
    MYPRINTF(("i_direct[%d]:%d\n",i,root_inode->i_direct[i]));
  }
#endif
  if (root_inode->i_blocks > VSFS_NUM_DIRECT) 
  {
    for(int i_block = 0;i_block < VSFS_NUM_DIRECT;i_block++)
    {  
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) 
        { 
          if (data_block[j].ino < fs->sb->num_inodes) 
          {
              if(strcmp(data_block[j].name,path+1) == 0) 
              {
                *ino = data_block[j].ino;
                MYPRINTF(("find file %s in %d inode\n",data_block[j].name,data_block[j].ino));
                return 0;
              }
          }
        }
    }
    if(root_inode->i_indirect < fs->sb->data_region)
    {
      return -ENOSYS;
    }
    vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_indirect);
    int block_out = root_inode->i_blocks - VSFS_NUM_DIRECT;
    for(int i = 0;i < block_out;i++) 
    {
      vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * data_block_ptr[i]);
      for (int j = 0;j < 16;j++) 
      { 
        if (data_block[j].ino < fs->sb->num_inodes) 
        {
              if(strcmp(data_block[j].name,path+1) == 0) 
              {
                *ino = data_block[j].ino;
                MYPRINTF(("find file %s in %d inode\n",data_block[j].name,data_block[j].ino));
                return 0;
              }
        }
      }
    }
  } else {
     for(int i_block = 0;i_block < (int)root_inode->i_blocks;i_block++){  
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) { 
          if (data_block[j].ino < fs->sb->num_inodes) {
              //MYPRINTF(("filename inode:%d\n",data_block[j].ino));
             // MYPRINTF(("filename name:%s\n",data_block[j].name));
             if(strcmp(data_block[j].name,path+1) == 0) {
                *ino = data_block[j].ino;
                MYPRINTF(("find file %s in %d inode\n",data_block[j].name,data_block[j].ino));
                return 0;
             }
          }
        }
     }
  }
  return -ENOSYS;
}
/*
  delete file from root
*/
static int
path_unlink_from_root(const char* path)
{
  if (path[0] != '/') {
    fprintf(stderr, "Not an absolute path\n");
    return -ENOSYS;
  }
  // TODO: complete this function and any helper functions
  if (strcmp(path, "/") == 0) {
    return 0;
  }
  fs_ctx* fs = get_fs();
  vsfs_inode *root_inode = &fs->itable[VSFS_ROOT_INO];

  if (root_inode->i_blocks > VSFS_NUM_DIRECT) 
  {
    for(int i_block = 0;i_block < VSFS_NUM_DIRECT;i_block++)
    {  
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) 
        { 
          if (data_block[j].ino < fs->sb->num_inodes) 
          {
              if(strcmp(data_block[j].name,path+1) == 0) 
              {
                MYPRINTF(("find file %s in %d inode dell\n",data_block[j].name,data_block[j].ino));
                data_block[j].ino = VSFS_INO_MAX;
                return 0;
              }
          }
        }
    }
    if(root_inode->i_indirect < fs->sb->data_region)
    {
      return -ENOSYS;
    }
    vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_indirect);
    int block_out = root_inode->i_blocks - VSFS_NUM_DIRECT;
    for(int i = 0;i < block_out;i++) 
    {
      vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * data_block_ptr[i]);
      for (int j = 0;j < 16;j++) 
      { 
        if (data_block[j].ino < fs->sb->num_inodes) 
        {
              if(strcmp(data_block[j].name,path+1) == 0) 
              {
                MYPRINTF(("find file %s in %d inode dell\n",data_block[j].name,data_block[j].ino));
                data_block[j].ino = VSFS_INO_MAX;
                return 0;
              }
        }
      }
    }
  } else {
     for(int i_block = 0;i_block < (int)root_inode->i_blocks;i_block++){  
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) { 
          if (data_block[j].ino < fs->sb->num_inodes) {
             if(strcmp(data_block[j].name,path+1) == 0) 
              {
                MYPRINTF(("find file %s in %d inode dell\n",data_block[j].name,data_block[j].ino));
                data_block[j].ino = VSFS_INO_MAX;
                return 0;
              }
          }
        }
     }
  }
  return -ENOSYS;
}

/*
  check validity of ino
*/
static int
inode_lookup(const vsfs_ino_t ino,vsfs_inode **vs_inode_in)
{
  if (vs_inode_in == NULL) {
    fprintf(stderr, "vs_inode_in is NULL\n");
    return -ENOSYS;
  }
  fs_ctx* fs = get_fs();
  vsfs_superblock* sb = fs->sb;
  if (ino >= sb->num_inodes) {
    fprintf(stderr, "ino >= sb->num_inodes\n");
    return -ENOSYS;
  }
  *vs_inode_in = &fs->itable[ino];
  return 0;
}
static int 
malloc_block_set_clear(uint32_t *dnode_index)
{
  fs_ctx* fs = get_fs();
  uint32_t dnode_index_new = 0;
  int ret = bitmap_alloc(fs->dbmap,fs->sb->num_blocks,&dnode_index_new);
  MYPRINTF(("-----bitmap_alloc--:%d %d ret:%d\n",dnode_index_new,fs->sb->num_blocks,ret));
  if (ret != 0)
    return -1;
  *dnode_index= dnode_index_new;
  bitmap_set(fs->dbmap,fs->sb->num_blocks,dnode_index_new,true);

  void *data_block_new_ptr = (void*)(fs->image + VSFS_BLOCK_SIZE * dnode_index_new);
  memset(data_block_new_ptr,0,VSFS_BLOCK_SIZE);
  return 0;
}
/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int
vsfs_statfs(const char* path, struct statvfs* st)
{
  (void)path; // unused
  fs_ctx* fs = get_fs();
  vsfs_superblock* sb = fs->sb; /* Get ptr to superblock from context */

  memset(st, 0, sizeof(*st));
  st->f_bsize = VSFS_BLOCK_SIZE;  /* Filesystem block size */
  st->f_frsize = VSFS_BLOCK_SIZE; /* Fragment size */
  // The rest of required fields are filled based on the information
  // stored in the superblock.
  st->f_blocks = sb->num_blocks;  /* Size of fs in f_frsize units */
  st->f_bfree = sb->free_blocks;  /* Number of free blocks */
  st->f_bavail = sb->free_blocks; /* Free blocks for unpriv users */
  st->f_files = sb->num_inodes;   /* Number of inodes */
  st->f_ffree = sb->free_inodes;  /* Number of free inodes */
  st->f_favail = sb->free_inodes; /* Free inodes for unpriv users */

  st->f_namemax = VSFS_NAME_MAX; /* Maximum filename length */
  MYPRINTF((" super sb:\n"));
  MYPRINTF((" File system size in bytes:%d\n",(int)sb->size));
  MYPRINTF((" Total number of inodes (set by mkfs):%d\n",(int)sb->num_inodes));
  MYPRINTF((" Number of available inodes:%d\n",(int)sb->free_inodes));
  MYPRINTF((" File system size in blocks:%d\n",(int)sb->num_blocks));
  MYPRINTF((" Number of available blocks in file system:%d\n",(int)sb->free_blocks));
  MYPRINTF((" First block after inode table:%d\n",(int)sb->data_region));
  return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the
 *       inode (for vsfs, that is the indirect block).
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int
vsfs_getattr(const char* path, struct stat* st)
{
  if (strlen(path) >= VSFS_PATH_MAX)
    return -ENAMETOOLONG;
  memset(st, 0, sizeof(*st));
  MYPRINTF(("vsfs_getattr - 111path:%s\n",path));
  vsfs_ino_t ino;
  vsfs_inode *inode;
#if 0
  fs_ctx* fs = get_fs();
  vsfs_superblock* sb = fs->sb; /* Get ptr to superblock from context */
  MYPRINTF((" super sb:\n"));
  MYPRINTF((" File system size in bytes:%d\n",(int)sb->size));
  MYPRINTF((" Total number of inodes (set by mkfs):%d\n",(int)sb->num_inodes));
  MYPRINTF((" Number of available inodes:%d\n",(int)sb->free_inodes));
  MYPRINTF((" File system size in blocks:%d\n",(int)sb->num_blocks));
  MYPRINTF((" Number of available blocks in file system:%d\n",(int)sb->free_blocks));
  MYPRINTF((" First block after inode table:%d\n",(int)sb->data_region));
#endif
  /* read inode meta */
  if(path_lookup(path,&ino) == 0) {
     /* get from inode number*/
     if(inode_lookup(ino,&inode) == 0) {
      st->st_mode = inode->i_mode;
      st->st_nlink = inode->i_nlink;
      st->st_size = inode->i_size; 
      st->st_blocks = inode->i_blocks;
      st->st_mtim = inode->i_mtime;
      return 0;
     }
  }
  return -ENOENT;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int
vsfs_readdir(const char* path,
             void* buf,
             fuse_fill_dir_t filler,
             off_t offset,
             struct fuse_file_info* fi)
{
  (void)offset;
  (void)fi;
  struct stat st;
  if(vsfs_getattr(path, &st) != 0)
    return -ENOSYS;
  if ((st.st_mode & S_IFDIR) == 0)
    return -ENOSYS;

  fs_ctx* fs = get_fs();
  vsfs_inode *root_inode = &fs->itable[VSFS_ROOT_INO];
  MYPRINTF(("vsfs_readdir - 111path:%s i_block:%d\n",path,root_inode->i_blocks));
  if (root_inode->i_blocks > VSFS_NUM_DIRECT) 
  {
    for(int i_block = 0;i_block < VSFS_NUM_DIRECT;i_block++)
    {  
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) 
        { 
          if (data_block[j].ino < fs->sb->num_inodes) 
          {
             filler(buf,data_block[j].name, NULL, 0);
          }
        }
    }
    if(root_inode->i_indirect < fs->sb->data_region)
    {
      MYPRINTF(("root_inode->i_indirect error:%d < %d\n",root_inode->i_indirect ,fs->sb->data_region));
      return -ENOSYS;
    }
    vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_indirect);
    int block_out = root_inode->i_blocks - VSFS_NUM_DIRECT;
    for(int i = 0;i < block_out;i++) 
    {
      vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * data_block_ptr[i]);
      for (int j = 0;j < 16;j++) 
      { 
        if (data_block[j].ino < fs->sb->num_inodes) 
        {
          filler(buf,data_block[j].name, NULL, 0);
        }
      }
    }
    return 0;
  } else {
     for(int i_block = 0;i_block < (int)root_inode->i_blocks;i_block++){  
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) { 
          if (data_block[j].ino < fs->sb->num_inodes) {
             filler(buf,data_block[j].name, NULL, 0);
          }
        }
     }
     return 0;
  }
  return -ENOSYS;
}

/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * You do NOT need to implement this function.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int
vsfs_mkdir(const char* path, mode_t mode)
{
  mode = mode | S_IFDIR;
  fs_ctx* fs = get_fs();
  MYPRINTF(("----------vsfs_mkdir--%s\n",path));
  // OMIT: create a directory at given path with given mode
  (void)path;
  (void)mode;
  (void)fs;
  return -ENOSYS;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * You do NOT need to implement this function.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int
vsfs_rmdir(const char* path)
{
  fs_ctx* fs = get_fs();
  MYPRINTF(("----------vsfs_rmdir--%s\n",path));
  // OMIT: remove the directory at given path (only if it's empty)
  (void)path;
  (void)fs;
  return -ENOSYS;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int
vsfs_create(const char* path, mode_t mode, struct fuse_file_info* fi)
{
  (void)fi; // unused
  MYPRINTF(("----------vsfs_create--%s\n",path));
  assert(S_ISREG(mode));
  fs_ctx* fs = get_fs();
  /* 
    apply for a new inode
  */
  // TODO: create a file at given path with given mode
  /* check if remaining space is enough */
  if(fs->sb->free_inodes == 0 || fs->sb->free_blocks == 0) {
    return -ENOSPC;
  }
  uint32_t inode_index;
  bool bit_ret = bitmap_alloc(fs->ibmap,fs->sb->num_inodes,&inode_index);
  if (bit_ret != false) {
     return -ENOSPC;
  }
  uint32_t dnode_index;
  bit_ret = bitmap_alloc(fs->dbmap,fs->sb->num_blocks,&dnode_index);
  if (bit_ret != false) {
     return -ENOSPC;
  }
  /* update vsfs_dentry */
  vsfs_inode *root_inode = &fs->itable[VSFS_ROOT_INO];
  if (root_inode->i_blocks > VSFS_NUM_DIRECT) 
  {
    for(int i_block = 0;i_block < VSFS_NUM_DIRECT;i_block++)
    {  
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) 
        { 
          if (data_block[j].ino > fs->sb->num_inodes) {
             data_block[j].ino = inode_index;
             strcpy(data_block[j].name,path+1);
             break;
          }
        }
    }
    if(root_inode->i_indirect < fs->sb->data_region)
    {
      return -ENOSYS;
    }
    vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_indirect);
    int block_out = root_inode->i_blocks - VSFS_NUM_DIRECT;
    for(int i = 0;i < block_out;i++) 
    {
      vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * data_block_ptr[i]);
      for (int j = 0;j < 16;j++) 
      { 
        if (data_block[j].ino < fs->sb->num_inodes) 
        {
          if (data_block[j].ino > fs->sb->num_inodes) 
          {
             data_block[j].ino = inode_index;
             strcpy(data_block[j].name,path+1);
             break;
          }
        }
      }
    }
  } else {
     for(int i_block = 0;i_block < (int)root_inode->i_blocks;i_block++) {
        vsfs_dentry *data_block = (vsfs_dentry*)(fs->image + VSFS_BLOCK_SIZE * root_inode->i_direct[i_block]);
        for (int j = 0;j < 16;j++) { 
          if (data_block[j].ino > fs->sb->num_inodes) {
             data_block[j].ino = inode_index;
             strcpy(data_block[j].name,path+1);
             break;
          }
        }
     }
  }
  vsfs_inode *new_inode = &fs->itable[inode_index];
  new_inode->i_blocks = 1;
  new_inode->i_mode = mode;
  new_inode->i_nlink = 1;
  new_inode->i_direct[0] = dnode_index;
  new_inode->i_size = 0;
  clock_gettime(CLOCK_REALTIME, &(new_inode->i_mtime));
  /* set bitmap */
  bitmap_set(fs->ibmap,fs->sb->num_inodes,inode_index,true);
  bitmap_set(fs->dbmap,fs->sb->num_blocks,dnode_index,true);
  fs->sb->free_blocks -= 1;
  fs->sb->free_inodes -= 1;
  return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int
vsfs_unlink(const char* path)
{
  fs_ctx* fs = get_fs();
  // TODO: remove the file at given path
  vsfs_ino_t ino;
  if(path_lookup(path,&ino) != 0)
    return -ENOSYS;
  vsfs_inode *inode;
  if(inode_lookup(ino,&inode) != 0) 
     return -ENOSYS;
  
  /* return space for datablock */
  if (inode->i_blocks <= VSFS_NUM_DIRECT) { /* no indirect blocks */
    MYPRINTF(("vsfs_unlink -- this is a small file:%d block\n",inode->i_blocks));
    for(vsfs_blk_t i= 0;i < inode->i_blocks;i++) {
      bitmap_set(fs->dbmap,fs->sb->num_blocks,inode->i_direct[i],false);
      inode->i_direct[i] = 0;
    }
  } else { 
    MYPRINTF(("vsfs_unlink -- this is a big file:%d block indirect:%d\n",inode->i_blocks,inode->i_indirect));
    if (inode->i_indirect < fs->sb->data_region) { /* system error */
      MYPRINTF(("i_indirect error:%d < %d\n",inode->i_indirect,fs->sb->data_region));
      return -ENOSYS;
    }
    vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * inode->i_indirect);
    vsfs_blk_t sencod_block_num = inode->i_blocks - VSFS_NUM_DIRECT;
    for(vsfs_blk_t i= 0;i < sencod_block_num;i++) {
        bitmap_set(fs->dbmap,fs->sb->num_blocks,data_block_ptr[i],false);
        data_block_ptr[i] = 0;
    }
    for(vsfs_blk_t i= 0;i < VSFS_NUM_DIRECT;i++) {
      bitmap_set(fs->dbmap,fs->sb->num_blocks,inode->i_direct[i],false);
      inode->i_direct[i] = 0;
    }
    bitmap_set(fs->dbmap,fs->sb->num_blocks,inode->i_indirect,false);
    inode->i_indirect = 0;
    fs->sb->free_blocks += 1;
  }

  fs->sb->free_blocks += inode->i_blocks;
  path_unlink_from_root(path);
  fs->sb->free_inodes += 1;

  bitmap_free(fs->ibmap,fs->sb->num_inodes,ino);
  memset(inode,0,sizeof(vsfs_inode));
  MYPRINTF(("vsfs_unlink -- release:ino %d\n",ino));
  return 0;
}

/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int
vsfs_utimens(const char* path, const struct timespec times[2])
{
  fs_ctx* fs = get_fs();
  vsfs_inode* ino = NULL;

  // TODO: update the modification timestamp (mtime) in the inode for given
  // path with either the time passed as argument or the current time,
  // according to the utimensat man page
  (void)path;
  (void)fs;
  (void)ino;

  // 0. Check if there is actually anything to be done.
  if (times[1].tv_nsec == UTIME_OMIT) {
    // Nothing to do.
    return 0;
  }

  // 1. TODO: Find the inode for the final component in path
  vsfs_ino_t ino_t;
  if(path_lookup(path,&ino_t) != 0)
    return -ENOSYS;

  if(inode_lookup(ino_t,&ino) != 0) 
     return -ENOSYS;
  // 2. Update the mtime for that inode.
  //    This code is commented out to avoid failure until you have set
  //    'ino' to point to the inode structure for the inode to update.
  if (times[1].tv_nsec == UTIME_NOW) {
    if (clock_gettime(CLOCK_REALTIME, &(ino->i_mtime)) != 0) {
     // clock_gettime should not fail, unless you give it a
     // bad pointer to a timespec.
     // assert(false);
    }
  } else {
     ino->i_mtime = times[1];
  }
  return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int
vsfs_truncate(const char* path, off_t size)
{
  if (strlen(path) >= VSFS_PATH_MAX)
    return -ENAMETOOLONG;
  fs_ctx* fs = get_fs();
  MYPRINTF(("-----vsfs_truncate------%s\n",path));
  // TODO: set new file size, possibly "zeroing out" the uninitialized range
  vsfs_ino_t ino;
  if(path_lookup(path,&ino) != 0)
    return -ENOSYS;

  vsfs_inode *inode;
  if(inode_lookup(ino,&inode) != 0) 
     return -ENOSYS;
  vsfs_blk_t block_num = inode->i_size/VSFS_BLOCK_SIZE;
  vsfs_blk_t block_num_offset = inode->i_size%VSFS_BLOCK_SIZE;
  vsfs_blk_t block_num_total = block_num+(block_num_offset==0?0:1);
  if(inode->i_size == 0) {
    block_num_total = 1;
  }
  vsfs_blk_t block_num_new = size/VSFS_BLOCK_SIZE;
  vsfs_blk_t block_num_offset_new = size%VSFS_BLOCK_SIZE;
  vsfs_blk_t block_num_total_new = block_num_new+(block_num_offset_new==0?0:1);
  if(size == 0) {
    block_num_total_new = 1;
  }
  vsfs_blk_t block_diff;
  if (block_num_total > block_num_total_new)
    block_diff  = block_num_total - block_num_total_new;
  else 
    block_diff  = block_num_total_new - block_num_total;
    
  MYPRINTF(("block_num_total:%d block_num_total_new:%d block_diff:%d\n",block_num_total,block_num_total_new,block_diff));
  MYPRINTF(("inode->i_size:%d size:%d\n",(int)inode->i_size,(int)size));
  if (block_diff == 0) {    /* size unchanged */
    inode->i_size = size; 
    clock_gettime(CLOCK_REALTIME, &(inode->i_mtime));
    return 0;
  } 
  if (inode->i_size > (uint64_t)size) {  /* smaller */
    if (block_num_total <= VSFS_NUM_DIRECT) { 
      MYPRINTF(("this is a small file:%d block\n",block_num_total));
      for(vsfs_blk_t i= 0;i < block_diff;i++) {
        bitmap_set(fs->dbmap,fs->sb->num_blocks,inode->i_direct[block_num_total-1-i],false);
        inode->i_direct[block_num_total-1-i] = 0;
      }
    } else {
      MYPRINTF(("this is a big file:%d block \n",block_num_total));
      if (inode->i_indirect < fs->sb->data_region) {
        MYPRINTF(("i_indirect error:%d < %d\n",inode->i_indirect,fs->sb->data_region));
        return -ENOSYS;
      }
      vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * inode->i_indirect);
      vsfs_blk_t sencod_block_num = block_num_total - VSFS_NUM_DIRECT;
      if (block_diff <= sencod_block_num) 
      {
        for(vsfs_blk_t i= 0;i < block_diff;i++) {
          bitmap_set(fs->dbmap,fs->sb->num_blocks,data_block_ptr[sencod_block_num-1-i],false);
          data_block_ptr[sencod_block_num-1-i] = 0;
       }
      } 
      else 
      {
        vsfs_blk_t sencod_block_num_diff = block_diff - sencod_block_num;
        for(vsfs_blk_t i= 0;i < sencod_block_num;i++) {
          bitmap_set(fs->dbmap,fs->sb->num_blocks,data_block_ptr[i],false);
          data_block_ptr[i] = 0;
        }

        for(vsfs_blk_t i= 0;i < sencod_block_num_diff;i++) {
        bitmap_set(fs->dbmap,fs->sb->num_blocks,inode->i_direct[VSFS_NUM_DIRECT-1-i],false);
        inode->i_direct[VSFS_NUM_DIRECT-1 - i] = 0;
        }
      }
      if (block_num_total_new < VSFS_NUM_DIRECT) {
        MYPRINTF(("big file to small file\n"));
        bitmap_set(fs->dbmap,fs->sb->num_blocks,inode->i_indirect,false);
        inode->i_indirect = 0;
        fs->sb->free_blocks += 1;
      }
    }
    /* update inode */
    inode->i_blocks -= block_diff;
    inode->i_size = size;
    clock_gettime(CLOCK_REALTIME, &(inode->i_mtime));

    fs->sb->free_blocks += block_diff;
    return 0;
  }
  /* bigger */
  if ((block_num_total_new > VSFS_NUM_DIRECT) &&
      (block_num_total  <= VSFS_NUM_DIRECT)) {
    if (block_diff+1 > fs->sb->free_blocks) {
      return -ENOSPC;
   }
  } else {
    if (block_diff > fs->sb->free_blocks) {
      return -ENOSPC;
    }
  }
  if (block_num_total_new <= VSFS_NUM_DIRECT) {  
      MYPRINTF(("block_num_total_new <= VSFS_NUM_DIRECT\n"));
      for(vsfs_blk_t i = 0;i < block_diff;i++) {
        malloc_block_set_clear(&inode->i_direct[block_num_total+i]); 
      }
  } else {
    MYPRINTF(("block_num_total_new > VSFS_NUM_DIRECT:this is a big file\n"));
    if(block_num_total < VSFS_NUM_DIRECT) 
    {
      MYPRINTF(("small file(%d block) to big file(%d block)\n",block_num_total,block_num_total_new));
      vsfs_blk_t diff_index = 0;
      vsfs_blk_t i_stat = block_num_total; 
      for(i_stat = block_num_total;i_stat < VSFS_NUM_DIRECT;i_stat++)
      {
        diff_index++;
        malloc_block_set_clear(&inode->i_direct[i_stat]); 
      }

      malloc_block_set_clear(&inode->i_indirect); 
      fs->sb->free_blocks -= 1;

      vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * inode->i_indirect);
      for(vsfs_blk_t i = 0;i < (block_diff-diff_index);i++)
      {
        malloc_block_set_clear(&data_block_ptr[i]);
      }
    } else {
      MYPRINTF(("big file(%d block) to more big file(%d block)\n",block_num_total,block_num_total_new));
      vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * inode->i_indirect);
      int diff_out = block_num_total - VSFS_NUM_DIRECT;
      for(vsfs_blk_t i = 0;i < block_diff;i++){
        malloc_block_set_clear(&data_block_ptr[diff_out+i]);
      }
    }
  }
  inode->i_blocks += block_diff;
  inode->i_size = size;
  clock_gettime(CLOCK_REALTIME, &(inode->i_mtime));
  fs->sb->free_blocks -= block_diff;
  return 0;
}
/* get offset */
static int 
vsfs_find_block_by_offset(vsfs_inode *inode,uint64_t offset,vsfs_blk_t *block_id)
{
   fs_ctx* fs = get_fs();
  vsfs_blk_t block_num = offset/VSFS_BLOCK_SIZE;
  vsfs_blk_t block_num_offset = offset%VSFS_BLOCK_SIZE;
  vsfs_blk_t block_num_total = block_num+(block_num_offset==0?0:1);
  if (offset == 0)
  {
    *block_id = inode->i_direct[0];
    return 0;
  }
  if (inode->i_size < offset)
  {
    fprintf(stderr, "inode->i_size;%d < offset:%d\n",(int)inode->i_size,(int)offset);
    return -1;
  }
  if (block_num_total > inode->i_blocks)
    return -1;
  if (inode->i_blocks <= VSFS_NUM_DIRECT) 
  {
    *block_id = inode->i_direct[block_num_total-1];
    return 0;
  } 
  else 
  {
    if(block_num_total <= VSFS_NUM_DIRECT) 
    {
        *block_id = inode->i_direct[block_num_total-1];
        return 0;
    } 
    else 
    {
      if (inode->i_indirect < fs->sb->data_region) {
        fprintf(stderr, "i_indirect error:%d < %d\n",inode->i_indirect,fs->sb->data_region);
        return -ENOSYS;
      }
      vsfs_blk_t *data_block_ptr = (vsfs_blk_t*)(fs->image + VSFS_BLOCK_SIZE * inode->i_indirect);
      vsfs_blk_t sencod_block_num = block_num_total - VSFS_NUM_DIRECT;
      *block_id = data_block_ptr[sencod_block_num-1];
      return 0;
    }
  }
}
/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int
vsfs_read(const char* path,
          char* buf,
          size_t size,
          off_t offset,
          struct fuse_file_info* fi)
{
  (void)fi; // unused
  fs_ctx* fs = get_fs();
  MYPRINTF(("-----vsfs_read------%s offset:%d size:%d\n",path,(int)offset,(int)size));
  // TODO: read data from the file at given offset into the buffer
  vsfs_ino_t ino;

  if(path_lookup(path,&ino) != 0)
    return -ENOSYS;

  vsfs_inode *inode;
  if(inode_lookup(ino,&inode) != 0) 
    return -ENOSYS;
  vsfs_blk_t block;
  vsfs_find_block_by_offset(inode,offset,&block);
  int offset_in_block = offset%VSFS_BLOCK_SIZE;
  void *data_block_ptr = (void*)(fs->image + VSFS_BLOCK_SIZE * block);
  if (offset+size < inode->i_size) 
  {
    memcpy(buf,data_block_ptr+offset_in_block,size);
    return size;
  }
  else 
  {
    int read_size  = inode->i_size - offset;
    memcpy(buf,data_block_ptr+offset_in_block,read_size);
    return read_size;
  }
  (void)path;
  (void)buf;
  (void)size;
  (void)offset;
  (void)fs;
  return -ENOSYS;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int
vsfs_write(const char* path,
           const char* buf,
           size_t size,
           off_t offset,
           struct fuse_file_info* fi)
{
  (void)fi; // unused
  fs_ctx* fs = get_fs();
  MYPRINTF(("----------vsfs_write--%s\n",path));
  // TODO: write data from the buffer into the file at given offset, possibly
  // "zeroing out" the uninitialized range
  vsfs_ino_t ino;

  if(path_lookup(path,&ino) != 0)
    return -ENOSYS;

  vsfs_inode *inode;
  if(inode_lookup(ino,&inode) != 0) 
    return -ENOSYS;

  if ((uint64_t)offset < inode->i_size)
  {
    MYPRINTF(("vsfs_write --- offset(%d) <= node->size(%d)\n",(int)offset,(int)inode->i_size));
    vsfs_blk_t block;
    vsfs_find_block_by_offset(inode,offset,&block);
    int offset_in_block = offset%VSFS_BLOCK_SIZE;
    void *data_block_ptr = (void*)(fs->image + VSFS_BLOCK_SIZE * block);
    memcpy(data_block_ptr+offset_in_block,buf,size);
    if (offset+size > inode->i_size)
        inode->i_size = offset+size;
    return size;
  } else {
    MYPRINTF(("vsfs_write --- offset(%d) > node->size(%d)\n",(int)offset,(int)inode->i_size));
    int file_size_want = offset+size;
    if(vsfs_truncate(path,file_size_want) != 0) {
      return -ENOSPC;
    }
    vsfs_blk_t block;
    vsfs_find_block_by_offset(inode,offset,&block);
    int offset_in_block = offset%VSFS_BLOCK_SIZE;
    void *data_block_ptr = (void*)(fs->image + VSFS_BLOCK_SIZE * block);
     memcpy(data_block_ptr+offset_in_block,buf,size);
     inode->i_size = offset+size;
     return size;
  }
  (void)path;
  (void)buf;
  (void)size;
  (void)offset;
  (void)fs;
  return -ENOSYS;
}

static struct fuse_operations vsfs_ops = {
  .destroy = vsfs_destroy,
  .statfs = vsfs_statfs,
  .getattr = vsfs_getattr,
  .readdir = vsfs_readdir,
  .mkdir = vsfs_mkdir,
  .rmdir = vsfs_rmdir,
  .create = vsfs_create,
  .unlink = vsfs_unlink,
  .utimens = vsfs_utimens,
  .truncate = vsfs_truncate,
  .read = vsfs_read,
  .write = vsfs_write,
};

int
main(int argc, char* argv[])
{
  vsfs_opts opts = { 0 }; // defaults are all 0
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (!vsfs_opt_parse(&args, &opts))
    return 1;

  fs_ctx fs = { 0 };
  if (!vsfs_init(&fs, &opts)) {
    fprintf(stderr, "Failed to mount the file system\n");
    return 1;
  }

  return fuse_main(args.argc, args.argv, &vsfs_ops, &fs);
}
