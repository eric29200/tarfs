#include <linux/buffer_head.h>

#include "tarfs.h"

/*
 * Read a file.
 */
ssize_t tarfs_file_read(struct file *file, char __user *userbuf, size_t count, loff_t *pos)
{
  struct inode *inode = file_inode(file);
  int block_pos, nb_chars, left;
  struct buffer_head *bh;
  sector_t block;
  
  /* adjust size */
  if (*pos + count > inode->i_size)
    count = inode->i_size - *pos;
  
  /* no more data to read */
  if (count <= 0)
    return 0;
  
  /* read block by block */
  for (left = count; left > 0;) {
    /* compute data block */
    block = (tarfs_i(inode)->entry->data_off + *pos) / inode->i_sb->s_blocksize;
    
    /* read block buffer */
    bh = sb_bread(inode->i_sb, block);
    if (!bh)
      goto out;
    
    /* find position and numbers of characters to read */
    block_pos = *pos % inode->i_sb->s_blocksize;
    nb_chars = inode->i_sb->s_blocksize - block_pos <= left ? inode->i_sb->s_blocksize - block_pos : left;
    
    /* copy to buffer */
    if (copy_to_user(userbuf, bh->b_data + block_pos, nb_chars)) {
      brelse(bh);
      return -EFAULT;
    }
    
    /* release block buffer */
    brelse(bh);
    
    /* update position */
    *pos += nb_chars;
    userbuf += nb_chars;
    left -= nb_chars;
  }
  
out:
  return count - left;
}

/*
 * TarFS file operations.
 */
struct file_operations tarfs_file_fops = {
  .llseek         = generic_file_llseek,
  .read           = tarfs_file_read,
};

/*
 * TarFS file inode operations.
 */
struct inode_operations tarfs_file_iops = {
};

