// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018  Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include<linux/fdtable.h>
#include <linux/buffer_head.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/ioctl.h>
#include<linux/uuid.h>

#include "ouichefs.h"
#include "ioctl_interface.h"

#define DEVICE_NAME "ouichefs"


int part_total = 0;
struct dentry_kobj tab_d_kobj[MAXMOUNT];
char uid_parse[8];


static ssize_t ouichfs_part_show(struct kobject *kobj, 
        struct kobj_attribute *attr, char *buf) 
{
		int i;
		for(i=0;i<MAXMOUNT;i++)
		{
			if (&tab_d_kobj[i].kobj_att == attr)
				break;
		}
      	struct super_block *sb = tab_d_kobj[i].kobj_dentry->d_sb;
		struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
        uint32_t nbr_inode = sbi->nr_inodes;
        uint32_t nbr_inode_2_hard_link = 0;
		struct inode* inode = NULL;
        unsigned long ino;
        for (ino = 0; ino < nbr_inode; ino++)
		{
                inode = ouichefs_iget(sb, ino);
                if (inode->i_nlink >= 2 && !S_ISDIR(inode->i_mode))
				{
                        nbr_inode_2_hard_link++;
                }
        }
		__u8 tab[UUID_SIZE]; 
		export_uuid(tab, &sb->s_uuid);
		for(i = 0; i < 16; ++i) {
			pr_warn("%d", tab[i]);
		}
		
        return snprintf(buf, PAGE_SIZE, "Nombre total inode = %d\n Nombre d'inode pointe par au moins 2 fichiers = %d\n", nbr_inode, nbr_inode_2_hard_link);
}


struct kobject *ouichfs_part;
/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{

	struct dentry *dentry = NULL;
	dentry = mount_bdev(fs_type, flags, dev_name, data,
			    ouichefs_fill_super);
	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else {
				int ret;
				char*  nombre = kmalloc(GFP_KERNEL,4*sizeof(char));
				sprintf(nombre,"%d",part_total);
				nombre[4] ='\0';
				tab_d_kobj[part_total].kobj_dentry = dentry;
				tab_d_kobj[part_total].kobj_att.attr.name = nombre;
				tab_d_kobj[part_total].kobj_att.attr.mode = 0600;
				tab_d_kobj[part_total].kobj_att.show = ouichfs_part_show;
				tab_d_kobj[part_total].kobj_att.store = NULL;

				ret = sysfs_create_file(ouichfs_part,&(tab_d_kobj[part_total].kobj_att.attr));
				if (ret<0)
					pr_info("fail to create sysfs\n");
                pr_info("'%s' mount success\n", dev_name);
				part_total++;
        }

	return dentry;
}



/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
	// Supression du sysFs
	int i,ret;
	for (i=0;i<MAXMOUNT;i++){
		if (tab_d_kobj[i].kobj_dentry->d_sb == sb){
				break;
		}	
	}
	if (i!=MAXMOUNT)
	{
	sysfs_remove_file(ouichfs_part,&(tab_d_kobj[i].kobj_att.attr));
	part_total--;
	}

	kill_block_super(sb);
	pr_info("unmounted disk\n");
}

static struct file_system_type ouichefs_file_system_type = {
	.owner = THIS_MODULE,
	.name = "ouichefs",
	.mount = ouichefs_mount,
	.kill_sb = ouichefs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
	.next = NULL,
};

// Définition de L'ioctl

static long ouichefs_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
	int ret = 0;
	pr_warn("aaaa\n");
	if(_IOC_TYPE(cmd) != MAGIC_IOCTL)
		return -EINVAL;

	switch(cmd) 
	{
		case OUICHEFSG_HARD_LIST:
			int fd =*((int*)arg); // récuperation du fd
			struct file *file = NULL;
			struct inode *inode = NULL;
			struct dentry* dentry = NULL;
			struct files_struct* files = current->files;
			if (files)
			{
				int lock_result = spin_trylock(&files->file_lock); // Can I run lockless? What files_fdtable() does?
				if (lock_result)
				{
					struct fdtable *fdt;
					fdt = files_fdtable(files);   // fdt is never NULL 
					if (fdt)
					{
						if (fd < fdt->max_fds)  // This can not be wrong in syscall open
						{
							file = fdt->fd[fd];  
						}
					}
					spin_unlock(&files->file_lock);
				}

			}
			if (file)
			{
				inode = file->f_inode; // on récupère l'inode 
				dentry = file->f_path.dentry; // On récupère la dentry 
			}
			
			unsigned long  ino = inode->i_ino;
			// On récupère le sb et on itère sur toute les inodes
			struct super_block *sb = dentry->d_sb;
			struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
			uint32_t nbr_inode = sbi->nr_inodes;
			struct inode* inode_parcours = NULL;
			struct buffer_head *bh = NULL;
			struct ouichefs_inode_info *ci_dir= NULL;
			struct ouichefs_dir_block *dblock = NULL;
			unsigned long ino_p;
			for (ino_p = 0; ino_p < nbr_inode; ino_p++) 
			{
					inode_parcours = ouichefs_iget(sb, ino_p);
					if (S_ISDIR(inode_parcours->i_mode)) 
					{
						// Parcourir tous les fichiers 
						ci_dir = OUICHEFS_INODE(inode_parcours);
						bh = sb_bread(sb, ci_dir->index_block);
						if (!bh)
							return ERR_PTR(-EIO);
						dblock = (struct ouichefs_dir_block *)bh->b_data;
						int i;
						for (i=0; i< OUICHEFS_MAX_SUBFILES;i++)
						{
							if (dblock->files[i].inode == ino)
								pr_info("%s \n",dblock->files[i].filename);
						}	
					}
			}			
		/*err_hard_list_2:
					if (task_st)
					put_task_struct(task_st);
		err_hard_list_1:
					if (pid)
						put_pid(pid);
					return ret;

				default:
					return -ENOTTY;*/
	}

	return ret;
}
static struct file_operations fops = {
    .unlocked_ioctl = ouichefs_ioctl
};
int major_allocated;

static int __init ouichefs_init(void)
{
	int ret;

	ret = ouichefs_init_inode_cache();
	if (ret) {
		pr_err("inode cache creation failed\n");
		goto end;
	}

	ret = register_filesystem(&ouichefs_file_system_type);
	if (ret) {
		pr_err("register_filesystem() failed\n");
		goto end;
	}
	// Creation du kernel object 
	if (!(ouichfs_part = kobject_create_and_add("ouichfs_part", kernel_kobj)))
	{
		pr_err("kobject_create_and_add failed\n");
    }
	// Ajout de l'ioctl 
	major_allocated = register_chrdev(0,DEVICE_NAME,&fops);
	pr_info("Le major %d\n",major_allocated);
	if (ret<0)
		 return ENOMEM;

	pr_info("module loaded\n");
end:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;
	int i;
	ret = unregister_filesystem(&ouichefs_file_system_type);
	if (ret)
		pr_err("unregister_filesystem() failed\n");

	ouichefs_destroy_inode_cache();

	for (i=0;i<part_total;i++)
    	kobject_put(ouichfs_part);

	unregister_chrdev(major_allocated,DEVICE_NAME);

	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@lip6.fr>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");
