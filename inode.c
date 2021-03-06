#include <linux/vfs.h>

#include "tarfs.h"

/*
 * Get a TarFS inode.
 */
struct inode *tarfs_iget(struct super_block *sb, ino_t ino)
{
  struct tar_entry *entry;
  struct inode *inode;
  
  /* try to get inode from cache or create a new one */
  inode = iget_locked(sb, ino);
  if (!inode)
    return ERR_PTR(-ENOMEM);
  
  /* inode is in cache : just return it */
  if (!(inode->i_state & I_NEW))
    return inode;
  
  /* check inode number */
  if (inode->i_ino < TARFS_ROOT_INO || inode->i_ino > tarfs_sb(inode->i_sb)->s_ninodes) {
    iget_failed(inode);
    return ERR_PTR(-EINVAL);
  }
  
  /* get tar entry */
  entry = tarfs_sb(inode->i_sb)->s_tar_entries[ino];
  if (!entry) {
    iget_failed(inode);
    return ERR_PTR(-EIO);
  }
  
  /* set inode */
  set_nlink(inode, 1);
  inode->i_mode = entry->mode;
  i_uid_write(inode, entry->uid);
  i_gid_write(inode, entry->gid);
  inode->i_size = entry->data_len;
  inode->i_atime = entry->atime;
  inode->i_mtime = entry->mtime;
  inode->i_ctime = entry->ctime;
  tarfs_i(inode)->entry = entry;
  
  /* set operations */
  if (S_ISDIR(inode->i_mode)) {
    inode->i_op = &tarfs_dir_iops;
    inode->i_fop = &tarfs_dir_fops;
    inode->i_mapping->a_ops = &tarfs_aops;
  } else if (S_ISLNK(inode->i_mode)) {
    inode->i_op = &tarfs_symlink_iops;
    inode->i_mapping->a_ops = &tarfs_aops;
  } else {
    inode->i_op = &tarfs_file_iops;
    inode->i_fop = &tarfs_file_fops;
    inode->i_mapping->a_ops = &tarfs_aops;
  }
  
  /* unlock inode */
  unlock_new_inode(inode);
  
  return inode;
}

/*
 * Get file attributes.
 */
int tarfs_getattr(struct user_namespace *mnt_userns, const struct path *path,
                  struct kstat *stat, u32 request_mask, unsigned int flags)
{
  struct super_block *sb = path->dentry->d_sb;
  struct inode *inode = d_inode(path->dentry);

  generic_fillattr(&init_user_ns, inode, stat);
  stat->blocks = inode->i_size / sb->s_blocksize;
  stat->blksize = sb->s_blocksize;

  return 0;
}
