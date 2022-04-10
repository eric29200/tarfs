#include "tarfs.h"

/*
 * Find an entry in a directory.
 */
static struct tar_entry *tarfs_find_entry(struct inode *dir, struct dentry *dentry)
{
  struct tar_entry *dir_entry, *child;
  struct list_head *pos;
  
  /* lookup in children */
  dir_entry = tarfs_i(dir)->entry;
  list_for_each(pos, &dir_entry->children) {
    child = list_entry(pos, struct tar_entry, list);
    if (strlen(child->name) == dentry->d_name.len && memcmp(child->name, dentry->d_name.name, dentry->d_name.len) == 0)
      return child;
  }
  
  return NULL;
}

/*
 * Lookup for a file in a directory.
 */
static struct dentry *tarfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
  struct inode *inode = NULL;
  struct tar_entry *entry;
  
  /* find entry and get inode */
  entry = tarfs_find_entry(dir, dentry);
  if (entry)
    inode = tarfs_iget(dir->i_sb, entry->ino);
  
  /* register inode - dentry */
  return d_splice_alias(inode, dentry);
}

/*
 * Get target link.
 */
static const char *tarfs_get_link(struct dentry *dentry, struct inode *inode, struct delayed_call *callback)
{
  struct tarfs_inode_info *tarfs_inode = tarfs_i(inode);
  
  /* inode must be a link */
  if (!S_ISLNK(inode->i_mode))
    return ERR_PTR(-ENOLINK);
  
  /* no link name */
  if (!tarfs_inode->entry || !tarfs_inode->entry->linkname)
    return ERR_PTR(-ENOLINK);
  
  /* return link name */
  return tarfs_inode->entry->linkname;
}

/*
 * TarFS directory inode operations.
 */
struct inode_operations tarfs_dir_iops = {
  .lookup         = tarfs_lookup,
};

/*
 * TarFS symlink inode operations.
 */
struct inode_operations tarfs_symlink_iops = {
  .get_link       = tarfs_get_link,
};
