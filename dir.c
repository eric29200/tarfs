#include "tarfs.h"

/*
 * Get directory entries.
 */
static int tarfs_readdir(struct file *file, struct dir_context *ctx)
{
  struct tar_entry *entry, *child;
  struct list_head *pos;
  loff_t i = 2;
  
  /* get tar entry */
  entry = tarfs_i(file->f_inode)->entry;
  if (!entry)
    return -ENOENT;
  
  /* emit '.' and '..' */
  if (!dir_emit_dots(file, ctx))
    return 0;
  
  /* emit all children */
  list_for_each(pos, &entry->children) {
    /* skip first entries */
    if (i++ < ctx->pos)
      continue;
    
    child = list_entry(pos, struct tar_entry, list);
    if (!dir_emit(ctx, child->name, strlen(child->name), child->ino, DT_UNKNOWN))
      break;
    
    /* update position */
    ctx->pos++;
  }
  
  return 0;
}

/*
 * TarFS directory file operations.
 */
struct file_operations tarfs_dir_fops = {
  .llseek                 = generic_file_llseek,
  .read                   = generic_read_dir,
  .iterate_shared         = tarfs_readdir,
};
