//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

int file_inode_exists(char *path)
{
    struct inode *ip;
    ip = namei(path);
    if(ip == 0){
        cprintf("file error: file not found\n");
        return 0;
    }
    switch (ip->type) {
        case T_FILE:
            return 1;
        case T_DIR:
            cprintf("file error: file is a directory\n");
            return 0;
        case T_DEV:
            cprintf("file error: file is a device\n");
            return 0;
        default:
            cprintf("file error: file is of unknown type\n");
            return 0;
    }
}

// this function, if file exists in path,
// will decrease its size to length by cutting off the end of the file
// or increase its size to length by adding 0s to the end of the file
int change_file_size(const char* path, int length) {
    if (length < 0) {
        cprintf("change_file_size: length is negative\n");
        return -1;
    }

    if (file_inode_exists(path) == 0) {
        return -1;
    }

    begin_op();
    struct inode *ip;
    ip = namei(path);
    ilock(ip);

    if (ip->size < length) {
        // increase file size
        int old_size = ip->size;
        char* buf = kalloc();
        memset(buf, 0, BSIZE);
        for (int i = old_size; i < length; i += BSIZE) {
            int n = length - i;
            n = n > BSIZE ? BSIZE : n;
            writei(ip, buf, i, n);
        }
        ip->size = length;
        iupdate(ip);
    }
    else if (ip->size > length) {
        // decrease file size
        ip->size = length;
        iupdate(ip);
    }
    iunlock(ip);
    end_op();
    return 0;
}
