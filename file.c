#include <linux/buffer_head.h>

#include "tarfs.h"

/*
 * Get a TarFS block.
 */
static int tarfs_get_block(struct inode *inode, sector_t block, struct buffer_head *bh_res, int create)
{
  struct tarfs_inode_info *tarfs_inode = tarfs_i(inode);
  unsigned long phys;

  /* check block number */
  if (block * inode->i_sb->s_blocksize > inode->i_size)
    return 0;

  /* map result buffer */
  phys = (tarfs_inode->entry->data_off / inode->i_sb->s_blocksize) + block;
  map_bh(bh_res, inode->i_sb, phys);

  return 0;
}

/*
 * Read full page of a file.
 */
static int tarfs_readpage(struct file *file, struct page *page)
{
  return block_read_full_page(page, tarfs_get_block);
}

/*
 * Get real block number of a block file.
 */
static sector_t tarfs_bmap(struct address_space *mapping, sector_t block)
{
  return generic_block_bmap(mapping, block, tarfs_get_block);
}

/*
 * TarFS file inode operations.
 */
struct inode_operations tarfs_file_iops = {
  .getattr        = tarfs_getattr,
};

/*
 * TarFS file operations.
 */
struct file_operations tarfs_file_fops = {
  .llseek         = generic_file_llseek,
  .read_iter      = generic_file_read_iter,
  .mmap           = generic_file_mmap,
  .splice_read    = generic_file_splice_read,
};

/*
 * TarFS address space operations.
 */
struct address_space_operations tarfs_aops = {
  .readpage       = tarfs_readpage,
  .bmap           = tarfs_bmap,
};
