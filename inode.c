// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/namei.h>

#include "ouichefs.h"
#include "bitmap.h"

static const struct inode_operations ouichefs_inode_ops;

/*
 * Get inode ino from disk.
 */
struct inode *ouichefs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode = NULL;
	struct ouichefs_inode *cinode = NULL;
	struct ouichefs_inode_info *ci = NULL;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct buffer_head *bh = NULL;
	uint32_t inode_block = (ino / OUICHEFS_INODES_PER_BLOCK) + 1;
	uint32_t inode_shift = ino % OUICHEFS_INODES_PER_BLOCK;
	int ret;

	/* Fail if ino is out of range */
	if (ino >= sbi->nr_inodes)
		return ERR_PTR(-EINVAL);

	/* Get a locked inode from Linux */
	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	/* If inode is in cache, return it */
	if (!(inode->i_state & I_NEW))
		return inode;

	ci = OUICHEFS_INODE(inode);
	/* Read inode from disk and initialize */
	bh = sb_bread(sb, inode_block);
	if (!bh) {
		ret = -EIO;
		goto failed;
	}
	cinode = (struct ouichefs_inode *)bh->b_data;
	cinode += inode_shift;

	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &ouichefs_inode_ops;

	inode->i_mode = le32_to_cpu(cinode->i_mode);
	i_uid_write(inode, le32_to_cpu(cinode->i_uid));
	i_gid_write(inode, le32_to_cpu(cinode->i_gid));
	inode->i_size = le32_to_cpu(cinode->i_size);
	inode->i_ctime.tv_sec = (time64_t)le32_to_cpu(cinode->i_ctime);
	inode->i_ctime.tv_nsec = 0;
	inode->i_atime.tv_sec = (time64_t)le32_to_cpu(cinode->i_atime);
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = (time64_t)le32_to_cpu(cinode->i_mtime);
	inode->i_mtime.tv_nsec = 0;
	inode->i_blocks = le32_to_cpu(cinode->i_blocks);
	set_nlink(inode, le32_to_cpu(cinode->i_nlink));

	ci->index_block = le32_to_cpu(cinode->index_block);

	if (S_ISDIR(inode->i_mode)) {
		inode->i_fop = &ouichefs_dir_ops;
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &ouichefs_file_ops;
		inode->i_mapping->a_ops = &ouichefs_aops;
	}

	brelse(bh);

	/* Unlock the inode to make it usable */
	unlock_new_inode(inode);

	return inode;

failed:
	brelse(bh);
	iget_failed(inode);
	return ERR_PTR(ret);
}


/*
 * Look for dentry in dir.
 * Fill dentry with NULL if not in dir, with the corresponding inode if found.
 * Returns NULL on success.
 */
static struct dentry *ouichefs_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct ouichefs_inode_info *ci_dir = OUICHEFS_INODE(dir);
	struct inode *inode = NULL;
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	struct ouichefs_file *f = NULL;
	int i;

	/* Check filename length */
	if (dentry->d_name.len > OUICHEFS_FILENAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	/* Read the directory index block on disk */
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return ERR_PTR(-EIO);
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for the file in directory */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		f = &dblock->files[i];
		if (!f->inode)
			break;
		if (!strncmp(f->filename, dentry->d_name.name,
			     OUICHEFS_FILENAME_LEN)) {
			inode = ouichefs_iget(sb, f->inode);
			break;
		}
	}
	brelse(bh);

	/* Update directory access time */
	dir->i_atime = current_time(dir);
	mark_inode_dirty(dir);

	/* Fill the dentry with the inode */
	d_add(dentry, inode);

	return NULL;
}


/*
 * Create a new inode in dir.
 */
static struct inode *ouichefs_new_inode(struct inode *dir, mode_t mode)
{
	struct inode *inode;
	struct ouichefs_inode_info *ci;
	struct super_block *sb;
	struct ouichefs_sb_info *sbi;
	uint32_t ino, bno;
	int ret;

	/* Check mode before doing anything to avoid undoing everything */
	if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode)) {
		pr_err("File type not supported (only directory and \
			regular files supported)\n");
		return ERR_PTR(-EINVAL);
	}

	/* Check if inodes are available */
	sb = dir->i_sb;
	sbi = OUICHEFS_SB(sb);
	if (sbi->nr_free_inodes == 0 || sbi->nr_free_blocks == 0)
		return ERR_PTR(-ENOSPC);

	/* Get a new free inode */
	ino = get_free_inode(sbi);
	if (!ino)
		return ERR_PTR(-ENOSPC);
	inode = ouichefs_iget(sb, ino);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto put_ino;
	}
	ci = OUICHEFS_INODE(inode);

	/* Get a free block for this new inode's index */
	bno = get_free_block(sbi);
	if (!bno) {
		ret = -ENOSPC;
		goto put_inode;
	}
	ci->index_block = bno;

	/* Initialize inode */
	inode_init_owner(inode, dir, mode);
	inode->i_blocks = 1;
	if (S_ISDIR(mode)) {
		inode->i_size = OUICHEFS_BLOCK_SIZE;
		inode->i_fop = &ouichefs_dir_ops;
		set_nlink(inode, 2); /* . and .. */
	} else if (S_ISREG(mode) || S_ISLNK(mode)) {
		if (S_ISLNK(mode))
			inode->i_size = sizeof(struct ouichefs_distant_link);
		else
			inode->i_size = 0;
		inode->i_fop = &ouichefs_file_ops;
		inode->i_mapping->a_ops = &ouichefs_aops;
		set_nlink(inode, 1);
	}

	inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);

	return inode;

put_inode:
	iput(inode);
put_ino:
	put_inode(sbi, ino);

	return ERR_PTR(ret);
}


/*
 * Create a file or directory in this way:
 *   - check filename length and if the parent directory is not full
 *   - create the new inode (allocate inode and blocks)
 *   - cleanup index block of the new inode
 *   - add new file/directory in parent index
 */
static int ouichefs_create(struct inode *dir, struct dentry *dentry,
			   umode_t mode, bool excl)
{
	struct super_block *sb;
	struct inode *inode;
	struct ouichefs_inode_info *ci_dir;
	struct ouichefs_dir_block *dblock;
	char *fblock;
	struct buffer_head *bh, *bh2;
	int ret = 0, i;

	/* Check filename length */
	if (strlen(dentry->d_name.name) > OUICHEFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	/* Read parent directory index */
	ci_dir = OUICHEFS_INODE(dir);
	sb = dir->i_sb;
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return -EIO;
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Check if parent directory is full */
	if (dblock->files[OUICHEFS_MAX_SUBFILES - 1].inode != 0) {
		ret = -EMLINK;
		goto end;
	}

	/* Get a new free inode */
	inode = ouichefs_new_inode(dir, mode);

	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto end;
	}

	/*
	 * Scrub index_block for new file/directory to avoid previous data
	 * messing with new file/directory.
	 */
	bh2 = sb_bread(sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh2) {
		ret = -EIO;
		goto iput;
	}
	fblock = (char *)bh2->b_data;
	memset(fblock, 0, OUICHEFS_BLOCK_SIZE);
	mark_buffer_dirty(bh2);
	brelse(bh2);

	/* Find first free slot in parent index and register new inode */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++)
		if (dblock->files[i].inode == 0)
			break;
	dblock->files[i].inode = inode->i_ino;
	strncpy(dblock->files[i].filename,
		dentry->d_name.name, OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Update stats and mark dir and new inode dirty */
	mark_inode_dirty(inode);
	dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
	if (S_ISDIR(mode))
		inode_inc_link_count(dir);
	mark_inode_dirty(dir);

	/* setup dentry */
	d_instantiate(dentry, inode);

	return 0;

iput:
	put_block(OUICHEFS_SB(sb), OUICHEFS_INODE(inode)->index_block);
	put_inode(OUICHEFS_SB(sb), inode->i_ino);
	iput(inode);
end:
	brelse(bh);
	return ret;
}


// Fonction de lien symbolique utilisée pour les liens distant
static int ouichefs_symlink(struct inode *dir, struct dentry *new_dentry,
				const char *pathname)
{
	// Recuperation de la dentry
	int error = 0;
	struct path path;
	struct inode *new_inode; // Inode du nouveau
	struct dentry *old_dentry;
	struct inode *inode;
	struct super_block *old, *new;
	struct buffer_head *bh;
	struct ouichefs_distant_link dist_link;
	char *fblock;
	mode_t creat_mode;
	int err;

	error = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if (error)
		return error;

	old_dentry = path.dentry;
	inode = d_inode(old_dentry);

	old = old_dentry->d_sb;
	new = new_dentry->d_sb;

	if (strncmp("ouichefs", old->s_type->name, 8) != 0
	|| strncmp("ouichefs", new->s_type->name, 8) != 0)
		return -EINVAL;

	// Update des temps de l'inode
	inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);
	inode_inc_link_count(inode); // Incrémenter le cpt de lien
	ihold(inode);
	mark_inode_dirty(inode);

	if (uuid_equal(&old->s_uuid, &new->s_uuid)) {
		pr_debug("Lien symbolique non-implementé");
		return -EINVAL;
	} else {
		creat_mode = S_IFLNK | 0777;
		err = ouichefs_create(dir, new_dentry, creat_mode, 0);

		if (err)
			return err;

		//distant link
		new_inode = new_dentry->d_inode;

		bh = sb_bread(new, OUICHEFS_INODE(new_inode)->index_block);
		if (!bh)
			return -EIO;

		uuid_copy(&dist_link.uuid, &old->s_uuid);
		dist_link.inode = inode->i_ino;
		fblock = (char *)bh->b_data;
		memcpy(fblock, (void *)&dist_link, sizeof(dist_link));
		mark_buffer_dirty(bh);
		brelse(bh);

		mark_inode_dirty(new_inode);
		mark_inode_dirty(inode);
	}
	path_put(&path);
	return 0;
}


/*
 * Fonction d'ajout du nouveau fichier créer par un link
 */
int ouichefs_add_link(struct dentry *new_dentry, struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct dentry *father = new_dentry->d_parent;
	struct inode *father_inode = d_inode(father);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(father_inode);
	struct buffer_head *bh_new = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	int i;

	// Vérification de la taille du nom
	if (strlen(new_dentry->d_name.name) > OUICHEFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	bh_new = sb_bread(sb, ci->index_block);
	if (!bh_new)
		return -EIO;

	dir_block = (struct ouichefs_dir_block *)bh_new->b_data;
	// Recherche du premier free-slot
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; ++i) {
		if (dir_block->files[i].inode == 0)
			break;
	}

	dir_block->files[i].inode = inode->i_ino;
	strncpy(dir_block->files[i].filename,
		new_dentry->d_name.name, OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh_new);
	brelse(bh_new);

	dput(new_dentry);

	return 0;
}


/*
 * Fonction de lien pour ouichfs
 */

static int ouichefs_link(struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);

	if ((S_ISDIR(inode->i_mode))) {
		pr_warn("lien hard non permis sur un repertoire");
		return -EINVAL;
	}

	// Update des temps de l'inode
	inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);
	inode_inc_link_count(inode); // Incrémenter le cpt de lien
	ihold(inode);

	if (ouichefs_add_link(dentry, inode) == 0) {
		dget(dentry);
		d_instantiate(dentry, inode);
	}

	return 0;
}


/*
 * Remove a link for a file. If link count is 0, destroy file in this way:
 *   - remove the file from its parent directory.
 *   - cleanup blocks containing data
 *   - cleanup file index block
 *   - cleanup inode
 */
static int ouichefs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct ouichefs_sb_info *sbi2, *sbi = OUICHEFS_SB(sb);
	struct inode *inode_file, *distant_inode, *inode = d_inode(dentry);
	struct buffer_head *bh = NULL, *bh2 = NULL, *bh3 = NULL, *bh4 = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	struct ouichefs_file_index_block *file_block = NULL, *file_block2 = NULL;
	struct ouichefs_distant_link *distant_link;
	uint32_t ino, bno, bno2;
	uint8_t has_distant_link;
	int i, f_id = -1, nr_subs = 0;

	ino = inode->i_ino;
	bno = OUICHEFS_INODE(inode)->index_block;

	if (S_ISLNK(inode->i_mode)) {
		bh = sb_bread(inode->i_sb, bno);
		distant_link = (struct ouichefs_distant_link *) bh->b_data;

		for (i = 0; i < part_total; ++i) {
			sbi = OUICHEFS_SB(tab_d_kobj[i].kobj_dentry->d_sb);

			if (uuid_equal(&(sbi->uuid), &distant_link->uuid)) {
				inode_file = ouichefs_iget(tab_d_kobj[i].kobj_dentry->d_sb,
							distant_link->inode);
				if (IS_ERR(inode_file))
					return PTR_ERR(inode_file);
				break;
			}
		}
		inode_dec_link_count(inode_file);
		mark_inode_dirty(inode_file);
	}

	/* Read parent directory index */
	bh = sb_bread(sb, OUICHEFS_INODE(dir)->index_block);
	if (!bh)
		return -EIO;

	dir_block = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for inode in parent index and get number of subfiles */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; ++i) {
		if (dir_block->files[i].inode == ino
				&& strcmp(dir_block->files[i].filename, dentry->d_name.name)
				== 0)
			f_id = i;
		else if (dir_block->files[i].inode == 0)
			break;
	}
	nr_subs = i;

	/* Remove file from parent directory */
	if (f_id != OUICHEFS_MAX_SUBFILES - 1)
		memmove(dir_block->files + f_id,
			dir_block->files + f_id + 1,
			(nr_subs - f_id - 1) * sizeof(struct ouichefs_file));
	memset(&dir_block->files[nr_subs - 1],
	       0, sizeof(struct ouichefs_file));
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Update inode stats */
	dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
	if (S_ISDIR(inode->i_mode)) {
		inode_dec_link_count(dir);
	} else {
		/**
		 * Partie 4, etape 8 structure, test si existence d'un lien distant 
		 * Ici on part de l'hypothese qu'un symlink ne doit pas augmenter le 
		 * compteur de reference, i_nlink doit donc seulement etre un compteur
		 * de lien locaux
		 * 	 
		 * Le code est sous commentaire car on obtiens une erreur.
		 * On a pas eu le temps de fix cette partie donc on la laisse
		 * comme ça pour avoir une trace de notre travail pour le rendu
		 * 
		 */
                /*
                has_distant_link = 0;
                if (inode->i_nlink == 1) {
                        for (i=0;i<part_total;i++) {
                                sbi2 = OUICHEFS_SB(tab_d_kobj[i].kobj_dentry->d_sb);
                                if (uuid_equal(&(sbi2->uuid), &sb->s_uuid) == 0) {
                                        for (j = 0; j < sbi2->nr_inodes; j++) {
                                                distant_inode = ouichefs_iget(tab_d_kobj[i].kobj_dentry->d_sb, inode->i_ino);
                                                if (IS_ERR(distant_inode)) {
                                                        return PTR_ERR(distant_inode);
                                                }
                                                if (S_ISLNK(distant_inode->i_mode)) {
                                                        bh = sb_bread(distant_inode->i_sb, OUICHEFS_INODE(distant_inode)->index_block);
                                                        distant_link = (struct ouichefs_distant_link*) bh->b_data;

                                                        if ((distant_link->inode == inode->i_ino) && (uuid_equal(&(distant_link->uuid), &(sb->s_uuid)))) {
                                                                has_distant_link = 1;
                                                                goto end_test_distant_link;
                                                        }
                                                }
                                        }
                                }
                        }
                }

end_test_distant_link:

                if (has_distant_link) {
                        // pr_info("Il faut faire une migration");
                        // pr_info("inode block = %d", inode->i_blocks);

                        distant_inode->i_mode = S_IFREG | 0777;

                        // Ecriture sur les blocks                        
                        
                        bh = sb_bread(sb, bno);
                        if (!bh) {
                                pr_err("bh error");
                                return -EIO;
                        }

                        file_block = (struct ouichefs_file_index_block *)bh->b_data;

                        bh2 = sb_bread(distant_inode->i_sb, OUICHEFS_INODE(distant_inode)->index_block);
                        if (!bh) {
                                pr_err("bh2 error");
                                return -EIO;
                        }

                        file_block2 = (struct ouichefs_file_index_block *)bh2->b_data;
                        memset(file_block2, 0, OUICHEFS_BLOCK_SIZE);

                        for (i = 0; i < inode->i_blocks; i++) {
                                
                                bh3 = sb_bread(sb, file_block->blocks[i]);
                                if (!bh3) {
                                        pr_err("bh3 error");
                                        return -EIO;
                                }
                                
                                if (file_block2->blocks[i] == 0) {
                                        pr_info("fais voir");
                                        bno2 = get_free_block(OUICHEFS_SB(distant_inode->i_sb));
                                        if (!bno2) {
                                                pr_err("bno2");
                                                return -ENOSPC;
                                        }
                                        file_block2->blocks[i] = bno2;
                                } 

                                bh4 = sb_bread(distant_inode->i_sb, file_block2->blocks[i]);
                                if (!bh4) {
                                        pr_err("bh4 error");
                                        return -EIO;
                                }

                                memcpy(bh4->b_data, bh3->b_data, OUICHEFS_BLOCK_SIZE);
                                mark_buffer_dirty(bh2);
                                mark_buffer_dirty(bh3);
                                mark_buffer_dirty(bh4);
                        }

                        distant_inode->i_size = inode->i_size;
                        distant_inode->i_blocks = inode->i_blocks;

                        mark_inode_dirty(distant_inode);
                        mark_inode_dirty(inode);
                }
                */
		inode_dec_link_count(inode);
	}

	mark_inode_dirty(dir);

	dput(dentry);

	if (inode->i_nlink != 0)
		return 0;

	/*
	 * Cleanup pointed blocks if unlinking a file. If we fail to read the
	 * index block, cleanup inode anyway and lose this file's blocks
	 * forever. If we fail to scrub a data block, don't fail (too late
	 * anyway), just put the block and continue.
	 */

	bh = sb_bread(sb, bno);
	if (!bh)
		goto clean_inode;
	file_block = (struct ouichefs_file_index_block *)bh->b_data;
	if (S_ISDIR(inode->i_mode))
		goto scrub;
	for (i = 0; i < inode->i_blocks - 1; ++i) {
		char *block;

		if (!file_block->blocks[i])
			continue;

		put_block(sbi, file_block->blocks[i]);
		bh2 = sb_bread(sb, file_block->blocks[i]);
		if (!bh2)
			continue;
		block = (char *)bh2->b_data;
		memset(block, 0, OUICHEFS_BLOCK_SIZE);
		mark_buffer_dirty(bh2);
		brelse(bh2);
	}

scrub:
	/* Scrub index block */
	memset(file_block, 0, OUICHEFS_BLOCK_SIZE);
	mark_buffer_dirty(bh);
	brelse(bh);

clean_inode:
	/* Cleanup inode and mark dirty */
	inode->i_blocks = 0;
	OUICHEFS_INODE(inode)->index_block = 0;
	inode->i_size = 0;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	inode->i_mode = 0;
	inode->i_ctime.tv_sec =
		inode->i_mtime.tv_sec =
		inode->i_atime.tv_sec = 0;
	mark_inode_dirty(inode);
	iput(inode);

	/* Free inode and index block from bitmap */
	put_block(sbi, bno);
	put_inode(sbi, ino);

	return 0;
}


static int ouichefs_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry,
			   unsigned int flags)
{
	struct super_block *sb = old_dir->i_sb;
	struct ouichefs_inode_info *ci_old = OUICHEFS_INODE(old_dir);
	struct ouichefs_inode_info *ci_new = OUICHEFS_INODE(new_dir);
	struct inode *src = d_inode(old_dentry);
	struct buffer_head *bh_old = NULL, *bh_new = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	int i, f_id = -1, new_pos = -1, ret, nr_subs, f_pos = -1;

	/* fail with these unsupported flags */
	if (flags & (RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	/* Check if filename is not too long */
	if (strlen(new_dentry->d_name.name) > OUICHEFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	/* Fail if new_dentry exists or if new_dir is full */
	bh_new = sb_bread(sb, ci_new->index_block);
	if (!bh_new)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh_new->b_data;
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		/* if old_dir == new_dir, save the renamed file position */
		if (new_dir == old_dir) {
			if (strncmp(dir_block->files[i].filename,
				    old_dentry->d_name.name,
				    OUICHEFS_FILENAME_LEN) == 0)
				f_pos = i;
		}
		if (strncmp(dir_block->files[i].filename,
			    new_dentry->d_name.name,
			    OUICHEFS_FILENAME_LEN) == 0) {
			ret = -EEXIST;
			goto relse_new;
		}
		if (new_pos < 0 && dir_block->files[i].inode == 0)
			new_pos = i;
	}
	/* if old_dir == new_dir, just rename entry */
	if (old_dir == new_dir) {
		strncpy(dir_block->files[f_pos].filename,
			new_dentry->d_name.name,
			OUICHEFS_FILENAME_LEN);
		mark_buffer_dirty(bh_new);
		ret = 0;
		goto relse_new;
	}

	/* If new directory is empty, fail */
	if (new_pos < 0) {
		ret = -EMLINK;
		goto relse_new;
	}

	/* insert in new parent directory */
	dir_block->files[new_pos].inode = src->i_ino;
	strncpy(dir_block->files[new_pos].filename,
		new_dentry->d_name.name,
		OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh_new);
	brelse(bh_new);

	/* Update new parent inode metadata */
	new_dir->i_atime = new_dir->i_ctime
		= new_dir->i_mtime = current_time(new_dir);
	if (S_ISDIR(src->i_mode))
		inode_inc_link_count(new_dir);
	mark_inode_dirty(new_dir);

	/* remove target from old parent directory */
	bh_old = sb_bread(sb, ci_old->index_block);
	if (!bh_old)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh_old->b_data;
	/* Search for inode in old directory and number of subfiles */
	for (i = 0; OUICHEFS_MAX_SUBFILES; i++) {
		if (dir_block->files[i].inode == src->i_ino)
			f_id = i;
		else if (dir_block->files[i].inode == 0)
			break;
	}
	nr_subs = i;

	/* Remove file from old parent directory */
	if (f_id != OUICHEFS_MAX_SUBFILES - 1)
		memmove(dir_block->files + f_id,
			dir_block->files + f_id + 1,
			(nr_subs - f_id - 1) * sizeof(struct ouichefs_file));
	memset(&dir_block->files[nr_subs - 1],
	       0, sizeof(struct ouichefs_file));
	mark_buffer_dirty(bh_old);
	brelse(bh_old);

	/* Update old parent inode metadata */
	old_dir->i_atime = old_dir->i_ctime
		= old_dir->i_mtime
		= current_time(old_dir);
	if (S_ISDIR(src->i_mode))
		inode_dec_link_count(old_dir);
	mark_inode_dirty(old_dir);

	return 0;

relse_new:
	brelse(bh_new);
	return ret;
}


static int ouichefs_mkdir(struct inode *dir, struct dentry *dentry,
			  umode_t mode)
{
	return ouichefs_create(dir, dentry, mode | S_IFDIR, 0);
}


static int ouichefs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = d_inode(dentry);
	struct buffer_head *bh;
	struct ouichefs_dir_block *dblock;

	/* If the directory is not empty, fail */
	if (inode->i_nlink > 2)
		return -ENOTEMPTY;
	bh = sb_bread(sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh)
		return -EIO;
	dblock = (struct ouichefs_dir_block *)bh->b_data;
	if (dblock->files[0].inode != 0) {
		brelse(bh);
		return -ENOTEMPTY;
	}
	brelse(bh);

	/* Remove directory with unlink */
	return ouichefs_unlink(dir, dentry);
}


static const struct inode_operations ouichefs_inode_ops = {
	.lookup = ouichefs_lookup,
	.create = ouichefs_create,
	.link = ouichefs_link,
	.symlink = ouichefs_symlink,
	.unlink = ouichefs_unlink,
	.mkdir  = ouichefs_mkdir,
	.rmdir  = ouichefs_rmdir,
	.rename = ouichefs_rename,
};
