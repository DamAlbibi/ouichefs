// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018  Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
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
#include <linux/syscalls.h>
#include <asm/special_insns.h>
#include <asm/nops.h>

#include "ouichefs.h"
#include "ioctl_interface.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@lip6.fr>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");

#define DEVICE_NAME "ouichefs"
#define MAXMOUNT 100
#define CR0_MASK 0x10000 /* masque pour le 16eme bit de CR0 */
#define SYSCALL_OFFSET_INSERT 335

static unsigned long sys_fun_addr = 0xffffffff811f9da0; /* sys_close_x86_64 */
static unsigned long *syscall_table_addr = NULL; /* adresse de la table syscall */

module_param(sys_fun_addr, long, 0660); /* pour mettre une autre adresse de syscall en param */

static int part_total = 0;
static u32 old_cr0, cr0;
static int major_allocated;

struct dentry_kobj
{
	struct dentry* kobj_dentry;
	struct kobj_attribute kobj_att;
};

struct dentry_kobj tab_d_kobj[MAXMOUNT];
struct kobject *ouichefs_part;
char uid_parse[8];

/**
 * force l'ecriture sur CR0, car la fonction du noyau fait une verification
 * du bit WP oblige de passer par l'assembleur pour bypass cette verification
 */
static inline void bypass_write_cr0(unsigned long new_CR0)
{
	/* repris du code de la fonction originelle */
	asm volatile("mov %0,%%cr0" : "+r" (new_CR0) : : "memory");
}

/* retrouver l'adresse de base de la table des syscall */
static void find_sc_table_addr(void)
{
	unsigned long i = PAGE_OFFSET;
	unsigned long **addr;

	while (i < ULONG_MAX) { /* on parcours l'espace du kernel */
		addr = (unsigned long **)i;
		if ((unsigned long *)(addr[__NR_close]) == (unsigned long *)sys_fun_addr) {
			syscall_table_addr = (unsigned long *)addr;
			return;
		}
		i += sizeof(void *);
	}
}

static ssize_t ouichefs_part_show(struct kobject *kobj, 
        struct kobj_attribute *attr, char *buf) 
{
      	struct super_block *sb;
		struct ouichefs_sb_info *sbi;
		struct inode* inode = NULL;
        unsigned long ino;
        uint32_t nbr_inode;
        uint32_t nbr_inode_2_hard_link;
		int i;
		__u8 tab[UUID_SIZE];

		for(i = 0; i < MAXMOUNT; ++i) {
			if (&tab_d_kobj[i].kobj_att == attr)
				break;
		}

		sb = tab_d_kobj[i].kobj_dentry->d_sb;
		sbi = OUICHEFS_SB(sb);
		nbr_inode = sbi->nr_inodes;
		nbr_inode_2_hard_link = 0;

        for(ino = 0; ino < nbr_inode; ino++) {
            inode = ouichefs_iget(sb, ino);
            if (inode->i_nlink >= 2 && !S_ISDIR(inode->i_mode))
                nbr_inode_2_hard_link++;
        }

		export_uuid(tab, &sb->s_uuid);
		for(i = 0; i < 16; ++i) {
			pr_warn("%d", tab[i]);
		}
		
        return snprintf(buf, PAGE_SIZE, "Nombre total inode = %d\n Nombre \
						d'inode pointe par au moins 2 fichiers = %d\n",
						nbr_inode, nbr_inode_2_hard_link);
}

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;
	char*  nombre;
	int ret;

	dentry = mount_bdev(fs_type, flags, dev_name, data,
			    ouichefs_fill_super);

	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else {
		nombre = kmalloc(GFP_KERNEL, 4 * sizeof(char));
		sprintf(nombre, "%d", part_total);
		nombre[4] = '\0';
		tab_d_kobj[part_total].kobj_dentry = dentry;
		tab_d_kobj[part_total].kobj_att.attr.name = nombre;
		tab_d_kobj[part_total].kobj_att.attr.mode = 0600;
		tab_d_kobj[part_total].kobj_att.show = ouichefs_part_show;
		tab_d_kobj[part_total].kobj_att.store = NULL;

		ret = sysfs_create_file(ouichefs_part, &(tab_d_kobj[part_total].kobj_att.attr));
		if (ret < 0)
			pr_info("fail to create sysfs\n");

        pr_info("'%s' mount success\n", dev_name);
		++part_total;
    }
	return dentry;
}



/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
	/* Supression du sysFs */
	int i;
	for (i = 0; i < MAXMOUNT; ++i) {
		if (tab_d_kobj[i].kobj_dentry->d_sb == sb)
			break;
	}
	if (i != MAXMOUNT) {
		sysfs_remove_file(ouichefs_part, &(tab_d_kobj[i].kobj_att.attr));
		--part_total;
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

/**
 * Définition de L'ioctl
 */
static long ouichefs_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
	int fd;
	struct super_block *sb;
	struct ouichefs_sb_info *sbi;
	struct files_struct* files;
	struct buffer_head *bh;
	struct ouichefs_inode_info *ci_dir;
	struct inode *inode = NULL;
	struct inode* inode_parcours;
	struct file *file = NULL;
	struct fdtable *fdt;
	struct ouichefs_dir_block *dblock;
	struct dentry* dentry = NULL;
	unsigned long ino;
	unsigned long ino_p;
	uint32_t nbr_inode;
	int i;
	int ret = 0;

	if (_IOC_TYPE(cmd) != MAGIC_IOCTL)
		return -EINVAL;

	switch(cmd) {
		case OUICHEFSG_HARD_LIST:
			fd =*((int*)arg); /* récuperation du fd */
			files = current->files;

			if (files) {
				// Can I run lockless? What files_fdtable() does?
				int lock_result = spin_trylock(&files->file_lock);

				if (lock_result) {
					fdt = files_fdtable(files);   /* fdt is never NULL */
					if (fdt) {
						/* This can not be wrong in syscall open */
						if (fd < fdt->max_fds)
							file = fdt->fd[fd];
					}
					spin_unlock(&files->file_lock);
				}
			}
			if (file) {
				inode = file->f_inode; /* on recupere l'inode */
				dentry = file->f_path.dentry; // On récupère la dentry 
			}
			
			/* On recupere le sb et on itère sur toute les inodes */
			sb = dentry->d_sb;
			ino = inode->i_ino;
			sbi = OUICHEFS_SB(sb);
			nbr_inode = sbi->nr_inodes;
			inode_parcours = NULL;
			bh = NULL;
			ci_dir= NULL;
			dblock = NULL;

			for (ino_p = 0; ino_p < nbr_inode; ++ino_p) {
				inode_parcours = ouichefs_iget(sb, ino_p);
				if (S_ISDIR(inode_parcours->i_mode)) {
					/* Parcourir tous les fichiers */
					ci_dir = OUICHEFS_INODE(inode_parcours);
					bh = sb_bread(sb, ci_dir->index_block);
					if (!bh)
						return -EIO;

					dblock = (struct ouichefs_dir_block *)bh->b_data;
					for (i = 0; i < OUICHEFS_MAX_SUBFILES; ++i) {
						if (dblock->files[i].inode == ino)
							pr_info("%s \n",dblock->files[i].filename);
					}	
				}
			}
			break;

		default:
			return -ENOTTY;			
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

static const struct file_operations fops = {
    .unlocked_ioctl = ouichefs_ioctl
};

asmlinkage long to_replace(int empty) {
	pr_warn("Bonjour ! Je ne sert a rien...\n");
	return 0;
}

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

	/* Creation du kernel object  */
	ouichefs_part = kobject_create_and_add("ouichefs_part", kernel_kobj);
	if (!ouichefs_part)
		pr_err("kobject_create_and_add failed\n");

	/* Ajout de l'ioctl */
	major_allocated = register_chrdev(0,DEVICE_NAME,&fops);
	pr_info("Le major %d\n", major_allocated);
	if (ret < 0)
		 return -ENOMEM;

	/* insertion du syscall */
	find_sc_table_addr(); /* chercher l'adresse de la table */

	/**
	 * 16eme bit, pour retirer la protection du registre sur des zones memoire
	 * protegees
	 */
	old_cr0 = native_read_cr0();
	cr0 = old_cr0;
	cr0 &= ~ CR0_MASK;
	bypass_write_cr0(cr0);

	/**
	 * TEST D'INSERTION D'@
	 * le test concerne sys_close
	 */
	pr_warn("addr from kallsyms: 0x%lx   |  addr from calculation: 0x%lx\n",
			sys_fun_addr, syscall_table_addr[__NR_close]);
	pr_warn("addr of new syscall: %p", to_replace);

	syscall_table_addr[SYSCALL_OFFSET_INSERT] = (unsigned long *)to_replace;
	bypass_write_cr0(old_cr0); /* restaurer la protection */


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

	for (i = 0; i < part_total; ++i)
    	kobject_put(ouichefs_part);

	unregister_chrdev(major_allocated,DEVICE_NAME);

	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);
