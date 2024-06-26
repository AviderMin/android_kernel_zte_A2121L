/* policyproc.c -- preprocess/postprocess policy as binary image
 *
 * Copyright 2017-2018 ZTE Corp.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Written by Jia Jia <jia.jia@zte.com.cn>
 */

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "policyproc.h"
#include "policyproc_inst.h"


/*
 * Macro Definition
 */
#undef PP_DEBUGFS
/* #undef PP_VMALLOC */
#ifndef PP_VMALLOC
#define PP_VMALLOC
#endif

#if defined(PP_VMALLOC)
#define pp_zalloc(size) vzalloc(size)
#define pp_free(ptr) vfree(ptr)
#else
#define pp_zalloc(size) kzalloc(size, GFP_KERNEL)
#define pp_free(ptr) kfree(ptr)
#endif /* PP_VMALLOC */

#define PP_MAPBIT 1ULL      /* a bit in the node bitmap */
#define PP_PERMS_LEN 8      /* perms length in avtab extended perms */
#define PP_PERMS_DELIM " "  /* perms delimiter in avtab extended perms */

#define POLICYDB_VERSION_MAX_SUPPORTED POLICYDB_VERSION_XPERMS_IOCTL

/*
 * Type Definition
 */
/*
 * Misc Type Definition
 */
typedef struct {
	uint32_t start_bit;
	uint64_t node_map;
} __packed policy_node_t;

typedef struct {
	uint32_t map_size;
	uint32_t high_bit;
	uint32_t node_count;
	policy_node_t *node_list;
} __packed policy_bitmap_t;

typedef struct {
	policy_bitmap_t types;
	policy_bitmap_t negset;
	uint32_t flags;
} __packed policy_type_set_t;

typedef struct {
	uint32_t name_len;
	uint32_t datums;
	char *name;
} __packed policy_perm_t;

typedef struct {
	uint32_t expr_type;
	uint32_t attr;
	uint32_t op;
	policy_bitmap_t names;
	policy_type_set_t type_names;
} __packed policy_expression_t;

typedef struct {
	uint32_t perms;
	uint32_t expression_num;
	policy_expression_t *expression_list;
} __packed policy_constraints_t;

typedef struct {
	uint32_t sens_num;
	uint32_t sens_low;
	uint32_t sens_high;
	policy_bitmap_t cat_low;
	policy_bitmap_t cat_high;
} __packed policy_exp_range_t;

typedef struct {
	uint32_t sens;
	policy_bitmap_t cat;
} __packed policy_exp_dfltlevel_t;

typedef struct {
	uint8_t specified;
	uint8_t driver;
	uint32_t perms[PP_PERMS_LEN];
} __packed policy_avtab_extended_perms_t;

typedef struct {
	uint16_t source_type;
	uint16_t target_type;
	uint16_t target_class;
	uint16_t specified;
	policy_avtab_extended_perms_t extended_perms;
	uint32_t datums;
} __packed policy_avtab_item_t;

typedef struct {
	uint32_t expr_type;
	uint32_t boolean;
} __packed policy_cond_expr_t;

typedef struct {
	uint32_t element_num;
	policy_avtab_item_t *av_list;
} __packed policy_av_list_t;

typedef struct {
	uint32_t cur_state;
	uint32_t expression_num;
	policy_cond_expr_t *expression_list;
	policy_av_list_t true_list;
	policy_av_list_t false_list;
} __packed policy_condlist_item_t;

typedef struct {
	uint32_t role;
	uint32_t role_type;
	uint32_t new_role;
	uint32_t tclass;
} __packed policy_roletrans_item_t;

typedef struct {
	uint32_t role;
	uint32_t new_role;
} __packed policy_roleallow_item_t;

typedef struct {
	uint32_t name_len;
	char *name;
	uint32_t stype;
	uint32_t ttype;
	uint32_t tclass;
	uint32_t otype;
} __packed policy_filenametrans_item_t;

typedef struct {
	uint32_t user;
	uint32_t role;
	uint32_t ctype;
	policy_exp_range_t exp_range;
} __packed policy_context_t;

typedef struct {
	uint32_t sid;
	policy_context_t con;
} __packed policy_ocon_isid_t;

typedef struct {
	uint32_t name_len;
	char *name;
	policy_context_t con_0;
	policy_context_t con_1;
} __packed policy_ocon_fs_t;

typedef struct {
	uint32_t protocol;
	uint32_t low_port;
	uint32_t high_port;
	policy_context_t con;
} __packed policy_ocon_port_t;

typedef struct {
	uint32_t name_len;
	char *name;
	policy_context_t con_0;
	policy_context_t con_1;
} __packed policy_ocon_netif_t;

typedef struct {
	uint32_t addr;
	uint32_t mask;
	policy_context_t con;
} __packed policy_ocon_node_t;

typedef struct {
	uint32_t behavior;
	uint32_t name_len;
	char *name;
	policy_context_t con;
} __packed policy_ocon_fsuse_t;

typedef struct {
	uint32_t addr_0;
	uint32_t addr_1;
	uint32_t addr_2;
	uint32_t addr_3;
	uint32_t mask_0;
	uint32_t mask_1;
	uint32_t mask_2;
	uint32_t mask_3;
	policy_context_t con;
} __packed policy_ocon_node6_t;

typedef struct {
	uint64_t subnet_prefix;
	uint32_t low_pkey;
	uint32_t high_pkey;
	policy_context_t con;
} __packed policy_ocon_ibpkey_t;

typedef struct {
	uint32_t dev_name_len;
	uint32_t port;
	char *dev_name;
	policy_context_t con;
} __packed policy_ocon_ibendport_t;

typedef struct {
	uint32_t name_len;
	char *name;
	uint32_t sclass;
	policy_context_t con;
} __packed policy_genfs_ocon_item_t;

typedef struct {
	uint32_t fstype_len;
	char *fstype;
	uint32_t ocon_num;
	policy_genfs_ocon_item_t *ocon_list;
} __packed policy_genfs_item_t;

typedef struct {
	uint32_t source_type;
	uint32_t target_type;
	uint32_t target_class;
	policy_exp_range_t target_range;
} __packed policy_range_item_t;

typedef struct {
	policy_bitmap_t attr_map;
} __packed policy_typeattr_map_item_t;

typedef struct {
	uint32_t name_len;
	char *name;
	uint16_t datums;
} __packed policy_appendix_types_t;

typedef struct {
	uint32_t name_len;
	char *name;
	uint32_t datums;
} __packed policy_appendix_perms_t;

typedef struct {
	uint32_t cname_len;
	char *cname;
	uint16_t cdatums;
	uint32_t perms_num;
	policy_appendix_perms_t *perms;
} __packed policy_appendix_classes_t;

/*
 * Policy Layout Definition
 */
typedef struct {
	uint32_t name_len;
	uint32_t datums;
	uint32_t primary_name_num;
	uint32_t element_num;
	char *name;
	policy_perm_t *perm_list;
} __packed policy_common_t;

typedef struct {
	uint32_t name_len;
	uint32_t common_name_len;
	uint32_t datums;
	uint32_t primary_name_num;
	uint32_t element_num;
	uint32_t constraints_num;
	char *name;
	char *common_name;
	policy_perm_t *perm_list;
	policy_constraints_t *constraints_list;
	uint32_t validatetrans_num;
	policy_constraints_t *validatetrans_list;
	uint32_t default_user;
	uint32_t default_role;
	uint32_t default_range;
	uint32_t default_type;
} __packed policy_classes_t;

typedef struct {
	uint32_t name_len;
	uint32_t datums;
	uint32_t bounds;
	char *name;
	policy_bitmap_t dominates;
	policy_bitmap_t types;
} __packed policy_roles_t;

typedef struct {
	uint32_t name_len;
	uint32_t datums;
	uint32_t properties;
	uint32_t bounds;
	char *name;
} __packed policy_types_t;

typedef struct {
	uint32_t name_len;
	uint32_t datums;
	uint32_t bounds;
	char *name;
	policy_bitmap_t roles;
	policy_exp_range_t exp_range;
	policy_exp_dfltlevel_t exp_dfltlevel;
} __packed policy_users_t;

typedef struct {
	uint32_t datums;
	uint32_t state;
	uint32_t name_len;
	char *name;
} __packed policy_bools_t;

typedef struct {
	uint32_t name_len;
	uint32_t isalias;
	char *name;
	policy_exp_dfltlevel_t level;
} __packed policy_levels_t;

typedef struct {
	uint32_t name_len;
	uint32_t datums;
	uint32_t isalias;
	char *name;
} __packed policy_cats_t;

typedef struct {
	uint32_t primary_name_num;
	uint32_t element_num;
	void *attr_list;
} __packed policy_symtab_t;

typedef struct {
	uint32_t element_num;
	policy_avtab_item_t *attr_list;
} __packed policy_avtab_t;

typedef struct {
	uint32_t element_num;
	policy_condlist_item_t *attr_list;
} __packed policy_condlist_t;

typedef struct {
	uint32_t element_num;
	policy_roletrans_item_t *attr_list;
} __packed policy_roletrans_t;

typedef struct {
	uint32_t element_num;
	policy_roleallow_item_t *attr_list;
} __packed policy_roleallow_t;

typedef struct {
	uint32_t element_num;
	policy_filenametrans_item_t *attr_list;
} __packed policy_filenametrans_t;

typedef struct {
	uint32_t element_num;
	uint32_t isid_num;
	policy_ocon_isid_t *isid_list;
	uint32_t fs_num;
	policy_ocon_fs_t *fs_list;
	uint32_t port_num;
	policy_ocon_port_t *port_list;
	uint32_t netif_num;
	policy_ocon_netif_t *netif_list;
	uint32_t node_num;
	policy_ocon_node_t *node_list;
	uint32_t fsuse_num;
	policy_ocon_fsuse_t *fsuse_list;
	uint32_t node6_num;
	policy_ocon_node6_t *node6_list;
	uint32_t ibpkey_num;
	policy_ocon_ibpkey_t *ibpkey_list;
	uint32_t ibendport_num;
	policy_ocon_ibendport_t *ibendport_list;
} __packed policy_ocontext_t;

typedef struct {
	uint32_t element_num;
	policy_genfs_item_t *attr_list;
} __packed policy_genfs_t;

typedef struct {
	uint32_t element_num;
	policy_range_item_t *attr_list;
} __packed policy_range_t;

typedef struct {
	uint32_t element_num;
	policy_typeattr_map_item_t *attr_list;
} __packed policy_typeattr_map_t;

typedef struct {
	uint32_t types_num;
	policy_appendix_types_t *types_list;
	uint32_t classes_num;
	policy_appendix_classes_t *classes_list;
} __packed policy_appendix_t;

typedef struct {
	uint32_t magic;
	uint32_t target_str_len;
	char *target_str;
	uint32_t version_num;
	uint32_t config;
	uint32_t sym_num;
	uint32_t ocon_num;
	policy_bitmap_t policycaps;
	policy_bitmap_t permissive_map;
	policy_symtab_t symtab_common;
	policy_symtab_t symtab_classes;
	policy_symtab_t symtab_roles;
	policy_symtab_t symtab_types;
	policy_symtab_t symtab_users;
	policy_symtab_t symtab_bools;
	policy_symtab_t symtab_levels;
	policy_symtab_t symtab_cats;
	policy_avtab_t avtab;
	policy_condlist_t condlist;
	policy_roletrans_t roletrans;
	policy_roleallow_t roleallow;
	policy_filenametrans_t filenametrans;
	policy_ocontext_t ocontext;
	policy_genfs_t genfs;
	policy_range_t range;
	policy_typeattr_map_t typeattr_map;
	policy_appendix_t appx;
} __packed policy_db_t;

enum Policy_Proc_Error_t {
	/* warning, could be ignored. */
	WARNING_HEADER_UNSUPPORTED = 1,
	/* add more here if need */
};

/*
 * Global Variable Definition
 */
/*
 * Misc Variable Definition
 */
static uint32_t pp_version_num = 0;
static uint32_t pp_perf_mode = 0;


/*
 * Function Declaration
 */
#if defined(PP_DEBUGFS)
static int pp_debugfs_preproc_open(struct inode *inode, struct file *filp);
static int pp_debugfs_postproc_open(struct inode *inode, struct file *filp);
static ssize_t pp_debugfs_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);
#endif /* PP_DEBUGFS */

static int pp_appendix_to_type(policy_appendix_t *appx, pp_avc_desc_t *desc);
static int pp_appendix_to_perm(policy_appendix_classes_t *appx, const char *pname, uint32_t *pdatums);
static int pp_appendix_to_class_helper(policy_appendix_classes_t *appx, pp_avc_desc_t *desc);
static int pp_appendix_to_class(policy_appendix_t *appx, pp_avc_desc_t *desc);
static int pp_appendix_to_avc(policy_appendix_t *appx, pp_avc_desc_t *list, size_t list_len);

static int pp_read_bitmap(struct policy_file *fp, policy_bitmap_t *map);
static int pp_read_type_set(struct policy_file *fp, policy_type_set_t *type_set);
static int pp_read_perm(struct policy_file *fp, policy_perm_t *perm);
static int pp_read_expr(struct policy_file *fp, policy_expression_t *expr);
static int pp_read_cons(struct policy_file *fp, policy_constraints_t *cons);
static int pp_read_expanded_range(struct policy_file *fp, policy_exp_range_t *range);
static int pp_read_expanded_dfltlevel(struct policy_file *fp, policy_exp_dfltlevel_t *level);
static int pp_read_extended_perms(struct policy_file *fp, policy_avtab_extended_perms_t *avtab_xperms);
static int pp_read_context(struct policy_file *fp, policy_context_t *con);
static int pp_read_common_helper(struct policy_file *fp, policy_common_t *common);
static int pp_read_common(struct policy_file *fp, policy_db_t *db);
static int pp_read_classes_helper(struct policy_file *fp, policy_classes_t *classes);
static int pp_read_classes(struct policy_file *fp, policy_db_t *db);
static int pp_read_roles_helper(struct policy_file *fp, policy_roles_t *roles);
static int pp_read_roles(struct policy_file *fp, policy_db_t *db);
static int pp_read_types_helper(struct policy_file *fp, policy_types_t *types);
static int pp_read_types(struct policy_file *fp, policy_db_t *db);
static int pp_read_users_helper(struct policy_file *fp, policy_users_t *users);
static int pp_read_users(struct policy_file *fp, policy_db_t *db);
static int pp_read_bools_helper(struct policy_file *fp, policy_bools_t *bools);
static int pp_read_bools(struct policy_file *fp, policy_db_t *db);
static int pp_read_levels_helper(struct policy_file *fp, policy_levels_t *levels);
static int pp_read_levels(struct policy_file *fp, policy_db_t *db);
static int pp_read_cats_helper(struct policy_file *fp, policy_cats_t *cats);
static int pp_read_cats(struct policy_file *fp, policy_db_t *db);
static int pp_read_avtab_item(struct policy_file *fp, policy_avtab_item_t *item);
static int pp_read_cond_expr(struct policy_file *fp, policy_cond_expr_t *expr);
static int pp_read_condlist_item(struct policy_file *fp, policy_condlist_item_t *item);
static int pp_read_roletrans_item(struct policy_file *fp, policy_roletrans_item_t *item);
static int pp_read_roleallow_item(struct policy_file *fp, policy_roleallow_item_t *item);
static int pp_read_filenametrans_item(struct policy_file *fp, policy_filenametrans_item_t *item);
static int pp_read_ocon_isid(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_fs(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_port(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_netif(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_node(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_fsuse(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_node6(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_ibpkey(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_ocon_ibendport(struct policy_file *fp, policy_ocontext_t *ocon);
static int pp_read_genfs_item_helper(struct policy_file *fp, policy_genfs_ocon_item_t *item);
static int pp_read_genfs_item(struct policy_file *fp, policy_genfs_item_t *item);
static int pp_read_range_item(struct policy_file *fp, policy_range_item_t *item);
static int pp_read_typeattr_map_item(struct policy_file *fp, policy_typeattr_map_item_t *item);
static int pp_read_appendix_type(struct policy_file *fp, policy_appendix_types_t *type);
static int pp_read_appendix_perm(struct policy_file *fp, policy_appendix_perms_t *perm);
static int pp_read_appendix_class(struct policy_file *fp, policy_appendix_classes_t *class);

static int pp_write_bitmap(policy_bitmap_t *map, struct policy_file *fp);
static int pp_write_type_set(policy_type_set_t *type_set, struct policy_file *fp);
static int pp_write_perm(policy_perm_t *perm, struct policy_file *fp);
static int pp_write_expr(policy_expression_t *expr, struct policy_file *fp);
static int pp_write_cons(policy_constraints_t *cons, struct policy_file *fp);
static int pp_write_expanded_range(policy_exp_range_t *range, struct policy_file *fp);
static int pp_write_expanded_dfltlevel(policy_exp_dfltlevel_t *level, struct policy_file *fp);
static int pp_write_extended_perms(policy_avtab_extended_perms_t *avtab_xperms, struct policy_file *fp);
static int pp_write_context(policy_context_t *con, struct policy_file *fp);
static int pp_write_common_helper(policy_common_t *common, struct policy_file *fp);
static int pp_write_common(policy_db_t *db, struct policy_file *fp);
static int pp_write_classes_helper(policy_classes_t *classes, struct policy_file *fp);
static int pp_write_classes(policy_db_t *db, struct policy_file *fp);
static int pp_write_roles_helper(policy_roles_t *roles, struct policy_file *fp);
static int pp_write_roles(policy_db_t *db, struct policy_file *fp);
static int pp_write_types_helper(policy_types_t *types, struct policy_file *fp);
static int pp_write_types(policy_db_t *db, struct policy_file *fp);
static int pp_write_users_helper(policy_users_t *users, struct policy_file *fp);
static int pp_write_users(policy_db_t *db, struct policy_file *fp);
static int pp_write_bools_helper(policy_bools_t *bools, struct policy_file *fp);
static int pp_write_bools(policy_db_t *db, struct policy_file *fp);
static int pp_write_levels_helper(policy_levels_t *levels, struct policy_file *fp);
static int pp_write_levels(policy_db_t *db, struct policy_file *fp);
static int pp_write_cats_helper(policy_cats_t *cats, struct policy_file *fp);
static int pp_write_cats(policy_db_t *db, struct policy_file *fp);
static int pp_write_avtab_item(policy_avtab_item_t *item, struct policy_file *fp);
static int pp_write_cond_expr(policy_cond_expr_t *expr, struct policy_file *fp);
static int pp_write_condlist_item(policy_condlist_item_t *item, struct policy_file *fp);
static int pp_write_roletrans_item(policy_roletrans_item_t *item, struct policy_file *fp);
static int pp_write_roleallow_item(policy_roleallow_item_t *item, struct policy_file *fp);
static int pp_write_filenametrans_item(policy_filenametrans_item_t *item, struct policy_file *fp);
static int pp_write_ocon_isid(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_fs(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_port(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_netif(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_node(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_fsuse(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_node6(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_ibpkey(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_ocon_ibendport(policy_ocontext_t *ocon, struct policy_file *fp);
static int pp_write_genfs_item_helper(policy_genfs_ocon_item_t *item, struct policy_file *fp);
static int pp_write_genfs_item(policy_genfs_item_t *item, struct policy_file *fp);
static int pp_write_range_item(policy_range_item_t *item, struct policy_file *fp);
static int pp_write_typeattr_map_item(policy_typeattr_map_item_t *item, struct policy_file *fp);

static int pp_free_bitmap(policy_bitmap_t *map);
static int pp_free_perm(policy_perm_t *perm);
static int pp_free_expr(policy_expression_t *expr);
static int pp_free_cons(policy_constraints_t *cons);
static int pp_free_expanded_range(policy_exp_range_t *range);
static int pp_free_expanded_dfltlevel(policy_exp_dfltlevel_t *level);
static int pp_free_context(policy_context_t *con);
static int pp_free_common_helper(policy_common_t *common);
static int pp_free_common(policy_db_t *db);
static int pp_free_classes_helper(policy_classes_t *classes);
static int pp_free_classes(policy_db_t *db);
static int pp_free_roles_helper(policy_roles_t *roles);
static int pp_free_roles(policy_db_t *db);
static int pp_free_types_helper(policy_types_t *types);
static int pp_free_types(policy_db_t *db);
static int pp_free_users_helper(policy_users_t *users);
static int pp_free_users(policy_db_t *db);
static int pp_free_bools_helper(policy_bools_t *bools);
static int pp_free_bools(policy_db_t *db);
static int pp_free_levels_helper(policy_levels_t *levels);
static int pp_free_levels(policy_db_t *db);
static int pp_free_cats_helper(policy_cats_t *cats);
static int pp_free_cats(policy_db_t *db);
static int pp_free_avtab_item(policy_avtab_item_t *item);
static int pp_free_cond_expr(policy_cond_expr_t *expr);
static int pp_free_condlist_item(policy_condlist_item_t *item);
static int pp_free_roletrans_item(policy_roletrans_item_t *item);
static int pp_free_roleallow_item(policy_roleallow_item_t *item);
static int pp_free_filenametrans_item(policy_filenametrans_item_t *item);
static int pp_free_ocon_isid(policy_ocontext_t *ocon);
static int pp_free_ocon_fs(policy_ocontext_t *ocon);
static int pp_free_ocon_port(policy_ocontext_t *ocon);
static int pp_free_ocon_netif(policy_ocontext_t *ocon);
static int pp_free_ocon_node(policy_ocontext_t *ocon);
static int pp_free_ocon_fsuse(policy_ocontext_t *ocon);
static int pp_free_ocon_node6(policy_ocontext_t *ocon);
static int pp_free_ocon_ibpkey(policy_ocontext_t *ocon);
static int pp_free_ocon_ibendport(policy_ocontext_t *ocon);
static int pp_free_genfs_item_helper(policy_genfs_ocon_item_t *item);
static int pp_free_genfs_item(policy_genfs_item_t *item);
static int pp_free_range_item(policy_range_item_t *item);
static int pp_free_typeattr_map_item(policy_typeattr_map_item_t *item);

static int pp_parse_header(struct policy_file *fp, policy_db_t *db);
static int pp_parse_policycaps(struct policy_file *fp, policy_db_t *db);
static int pp_parse_permissive_map(struct policy_file *fp, policy_db_t *db);
static int pp_parse_symtab(struct policy_file *fp, policy_db_t *db);
static int pp_parse_avtab(struct policy_file *fp, policy_db_t *db);
static int pp_parse_condlist(struct policy_file *fp, policy_db_t *db);
static int pp_parse_roletrans(struct policy_file *fp, policy_db_t *db);
static int pp_parse_roleallow(struct policy_file *fp, policy_db_t *db);
static int pp_parse_filenametrans(struct policy_file *fp, policy_db_t *db);
static int pp_parse_ocontext(struct policy_file *fp, policy_db_t *db);
static int pp_parse_genfs(struct policy_file *fp, policy_db_t *db);
static int pp_parse_range(struct policy_file *fp, policy_db_t *db);
static int pp_parse_typeattr_map(struct policy_file *fp, policy_db_t *db);
static int pp_parse_policy(struct policy_file *fp, policy_db_t *db);
static int pp_parse_appendix(struct policy_file *fp, policy_appendix_t *appx);

static int pp_build_header(policy_db_t *db, struct policy_file *fp);
static int pp_build_policycaps(policy_db_t *db, struct policy_file *fp);
static int pp_build_permissive_map(policy_db_t *db, struct policy_file *fp);
static int pp_build_symtab(policy_db_t *db, struct policy_file *fp);
static int pp_build_avtab(policy_db_t *db, struct policy_file *fp);
static int pp_build_condlist(policy_db_t *db, struct policy_file *fp);
static int pp_build_roletrans(policy_db_t *db, struct policy_file *fp);
static int pp_build_roleallow(policy_db_t *db, struct policy_file *fp);
static int pp_build_filenametrans(policy_db_t *db, struct policy_file *fp);
static int pp_build_ocontext(policy_db_t *db, struct policy_file *fp);
static int pp_build_genfs(policy_db_t *db, struct policy_file *fp);
static int pp_build_range(policy_db_t *db, struct policy_file *fp);
static int pp_build_typeattr_map(policy_db_t *db, struct policy_file *fp);
static int pp_build_policy(policy_db_t *db, struct policy_file *fp);

static int pp_free_header(policy_db_t *db);
static int pp_free_policycaps(policy_db_t *db);
static int pp_free_permissive_map(policy_db_t *db);
static int pp_free_symtab(policy_db_t *db);
static int pp_free_avtab(policy_db_t *db);
static int pp_free_condlist(policy_db_t *db);
static int pp_free_roletrans(policy_db_t *db);
static int pp_free_roleallow(policy_db_t *db);
static int pp_free_filenametrans(policy_db_t *db);
static int pp_free_ocontext(policy_db_t *db);
static int pp_free_genfs(policy_db_t *db);
static int pp_free_range(policy_db_t *db);
static int pp_free_typeattr_map(policy_db_t *db);
static int pp_free_policy(policy_db_t *db);
static int pp_free_appendix(policy_appendix_t *appx);

static const char *pp_class_to_name(policy_db_t *db, uint32_t class);
static const char *pp_type_to_name(policy_db_t *db, uint32_t type);

static uint32_t pp_name_to_class(policy_db_t *db, const char *name);
static uint32_t pp_name_to_type(policy_db_t *db, const char *name);
static uint32_t pp_name_to_perm(policy_db_t *db, pp_avc_desc_t *desc, const char *name);

static bool pp_find_common(policy_db_t *db, pp_avc_desc_t *desc, size_t *index);
static bool pp_find_class(policy_db_t *db, pp_avc_desc_t *desc, size_t *index);
static bool pp_find_avc(policy_db_t *db, pp_avc_desc_t *desc, size_t *index);

static bool pp_match_permissive(policy_db_t *db, pp_perm_desc_t *desc, size_t *index);
static bool pp_match_avc(policy_db_t *db, pp_avc_desc_t *desc, size_t *index);

static bool pp_check_context(policy_db_t *db, pp_avc_desc_t *desc);
static bool pp_check_datums(policy_db_t *db, size_t index, pp_avc_desc_t *desc);

static int pp_new_permissive(policy_db_t *db, pp_perm_desc_t *desc, size_t *len);
static int pp_new_avc(policy_db_t *db, pp_avc_desc_t *desc, size_t *len);

static int pp_del_permissive(policy_db_t *db, size_t index, pp_perm_desc_t *desc, size_t *len);
static int pp_del_avc(policy_db_t *db, size_t index, pp_avc_desc_t *desc, size_t *len);

static int pp_mod_avc(policy_db_t *db, size_t index, pp_avc_desc_t *desc);
static int pp_unmod_avc(policy_db_t *db, size_t index, pp_avc_desc_t *desc);

static int pp_preproc_permissive(policy_db_t *db, pp_perm_desc_t *list, size_t list_len, size_t *perm_len);
static int pp_preproc_avc(policy_db_t *db, pp_avc_desc_t *list, size_t list_len, size_t *avc_len);

static int pp_postproc_permissive(policy_db_t *db, pp_perm_desc_t *list, size_t list_len, size_t *perm_len);
static int pp_postproc_avc(policy_db_t *db, pp_avc_desc_t *list, size_t list_len, size_t *avc_len);


/*
 * Global Variable Definition
 */
#if defined(PP_DEBUGFS)
static DEFINE_MUTEX(pp_debugfs_mutex);

static struct policy_file pp_debugfs_preproc_file;
static struct policy_file pp_debugfs_postproc_file;

static const struct file_operations pp_debugfs_preproc_fops = {
	.open = pp_debugfs_preproc_open,
	.read = pp_debugfs_read,
};

static const struct file_operations pp_debugfs_postproc_fops = {
	.open = pp_debugfs_postproc_open,
	.read = pp_debugfs_read,
};
#endif /* PP_DEBUGFS */

static int (*pp_symtab_read_map[SYM_NUM]) (struct policy_file *fp, policy_db_t *db) = {
	pp_read_common,
	pp_read_classes,
	pp_read_roles,
	pp_read_types,
	pp_read_users,
	pp_read_bools,
	pp_read_levels,
	pp_read_cats,
};

static int (*pp_symtab_write_map[SYM_NUM]) (policy_db_t *db, struct policy_file *fp) = {
	pp_write_common,
	pp_write_classes,
	pp_write_roles,
	pp_write_types,
	pp_write_users,
	pp_write_bools,
	pp_write_levels,
	pp_write_cats,
};

static int (*pp_symtab_free_map[SYM_NUM]) (policy_db_t *db) = {
	pp_free_common,
	pp_free_classes,
	pp_free_roles,
	pp_free_types,
	pp_free_users,
	pp_free_bools,
	pp_free_levels,
	pp_free_cats,
};


/*
 * Function Definition
 */
#if defined(PP_DEBUGFS)
static int pp_debugfs_preproc_open(struct inode *inode, struct file *filp)
{
	mutex_lock(&pp_debugfs_mutex);
	filp->private_data = &pp_debugfs_preproc_file;
	mutex_unlock(&pp_debugfs_mutex);

	return 0;
}

static int pp_debugfs_postproc_open(struct inode *inode, struct file *filp)
{
	mutex_lock(&pp_debugfs_mutex);
	filp->private_data = &pp_debugfs_postproc_file;
	mutex_unlock(&pp_debugfs_mutex);

	return 0;
}

static ssize_t pp_debugfs_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct policy_file *file = filp->private_data;
	int rc;

	mutex_lock(&pp_debugfs_mutex);
	rc = simple_read_from_buffer(buf, count, ppos, file->data, file->len);
	mutex_unlock(&pp_debugfs_mutex);

	return rc;
}
#endif /* PP_DEBUGFS */

static int pp_appendix_to_type(policy_appendix_t *appx, pp_avc_desc_t *desc)
{
	uint32_t i;
	policy_appendix_types_t *list = appx->types_list;
	int rc = -EINVAL;

	desc->sdatums = 0;
	desc->tdatums = 0;

	for (i = 0; i < appx->types_num; ++i) {
		if (!strcmp(list[i].name, desc->sname)) {
			desc->sdatums = list[i].datums;
		}

		if (!strcmp(list[i].name, desc->tname)) {
			desc->tdatums = list[i].datums;
		}

		if (desc->sdatums != 0 && desc->tdatums != 0) {
			rc = 0;
			break;
		}
	}

	return rc;
}

static int pp_appendix_to_perm(policy_appendix_classes_t *appx, const char *pname, uint32_t *pdatums)
{
	uint32_t i;
	policy_appendix_perms_t *list = appx->perms;
	int rc = -EINVAL;

	for (i = 0; i < appx->perms_num; ++i) {
		if (!strcmp(list[i].name, pname)) {
			*pdatums = list[i].datums;
			rc = 0;
			break;
		}
	}

	return rc;
}

static int pp_appendix_to_class_helper(policy_appendix_classes_t *appx, pp_avc_desc_t *desc)
{
	uint32_t i;
	char *buf = NULL;
	char *orig_bufp = NULL;
	const char *pname = NULL;
	int rc = 0;

	orig_bufp = buf = pp_zalloc(strlen(desc->pname) + 1);
	if (!buf) {
		pr_err("SELinux: failed to allocate memory\n");
		return -ENOMEM;
	}
	memcpy(buf, desc->pname, strlen(desc->pname));

	for (i = 0; i < PP_DATUMS_NUM; ++i) {
		pname = strsep(&buf, PP_PERMS_DELIM);
		if (!pname || !*pname) {
			break;
		}

		rc = pp_appendix_to_perm(appx, pname, &desc->pdatums[i]);
		if (rc) {
			break;
		}
	}
	if (orig_bufp) {
		pp_free(orig_bufp);
		orig_bufp = NULL;
		buf = NULL;
	}

	return rc;
}

static int pp_appendix_to_class(policy_appendix_t *appx, pp_avc_desc_t *desc)
{
	uint32_t i;
	policy_appendix_classes_t *list = appx->classes_list;
	int rc = -EINVAL;

	desc->cdatums = 0;
	memset(desc->pdatums, 0, PP_DATUMS_NUM * sizeof(uint32_t));

	for (i = 0; i < appx->classes_num; ++i) {
		if (!strcmp(list[i].cname, desc->cname)) {
			desc->cdatums = list[i].cdatums;
			rc = pp_appendix_to_class_helper(&list[i], desc);
			if (!rc) {
				break;
			}
		}
	}

	return rc;
}

static int pp_appendix_to_avc(policy_appendix_t *appx, pp_avc_desc_t *list, size_t list_len)
{
	uint32_t i;
	int rc = 0;

	for (i = 0; i < list_len; ++i) {
		rc = pp_appendix_to_type(appx, &list[i]);
		if (rc) {
			break;
		}

		rc = pp_appendix_to_class(appx, &list[i]);
		if (rc) {
			break;
		}
	}

	return rc;
}

static int pp_read_bitmap(struct policy_file *fp, policy_bitmap_t *map)
{
	uint32_t i;
	int rc;

	rc = next_entry(&(map->map_size), fp, sizeof(map->map_size));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(map->high_bit), fp, sizeof(map->high_bit));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(map->node_count), fp, sizeof(map->node_count));
	if (rc) {
		return rc;
	}

	if (!map->node_count) {
		return 0;
	}

	map->node_list = pp_zalloc(map->node_count * sizeof(policy_node_t));
	if (!map->node_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"node list of length %u\n", (uint32_t)(map->node_count * sizeof(policy_node_t)));
		return -ENOMEM;
	}

	for (i = 0; i < map->node_count; ++i) {
		rc = next_entry(&(map->node_list[i].start_bit), fp, sizeof(map->node_list[i].start_bit));
		if (rc) {
			goto pp_read_bitmap_fail;
		}

		rc = next_entry(&(map->node_list[i].node_map), fp, sizeof(map->node_list[i].node_map));
		if (rc) {
			goto pp_read_bitmap_fail;
		}
	}

	return 0;

pp_read_bitmap_fail:

	if (map->node_list) {
		pp_free(map->node_list);
		map->node_list = NULL;
	}

	return rc;
}

static int pp_read_type_set(struct policy_file *fp, policy_type_set_t *type_set)
{
	int rc;

	rc = pp_read_bitmap(fp, &(type_set->types));
	if (rc) {
		return rc;
	}

	rc = pp_read_bitmap(fp, &(type_set->negset));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(type_set->flags), fp, sizeof(type_set->flags));
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_perm(struct policy_file *fp, policy_perm_t *perm)
{
	int rc;

	rc = next_entry(&(perm->name_len), fp, sizeof(perm->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(perm->datums), fp, sizeof(perm->datums));
	if (rc) {
		return rc;
	}

	if (!perm->name_len) {
		return 0;
	}

	perm->name = pp_zalloc(perm->name_len + 1);
	if (!perm->name) {
		pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", perm->name_len + 1);
		return -ENOMEM;
	}

	rc = next_entry(perm->name, fp, perm->name_len);
	if (rc) {
		goto pp_read_perm_fail;
	}

	return 0;

pp_read_perm_fail:

	if (perm->name) {
		pp_free(perm->name);
		perm->name = NULL;
	}

	return rc;
}

static int pp_read_expr(struct policy_file *fp, policy_expression_t *expr)
{
	int rc;

	rc = next_entry(&(expr->expr_type), fp, sizeof(expr->expr_type));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(expr->attr), fp, sizeof(expr->attr));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(expr->op), fp, sizeof(expr->op));
	if (rc) {
		return rc;
	}

	if (le32_to_cpu(expr->expr_type) == CEXPR_NAMES) {
		rc = pp_read_bitmap(fp, &(expr->names));
		if (rc) {
			return rc;
		}

		if (pp_version_num >= POLICYDB_VERSION_CONSTRAINT_NAMES) {
			rc = pp_read_type_set(fp, &(expr->type_names));
			if (rc) {
				return rc;
			}
		}
	}

	return 0;
}

static int pp_read_cons(struct policy_file *fp, policy_constraints_t *cons)
{
	uint32_t i;
	int rc;

	rc = next_entry(&(cons->perms), fp, sizeof(cons->perms));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(cons->expression_num), fp, sizeof(cons->expression_num));
	if (rc) {
		return rc;
	}

	if (!cons->expression_num) {
		return 0;
	}

	cons->expression_list = pp_zalloc(cons->expression_num * sizeof(policy_expression_t));
	if (!cons->expression_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"expression list of length %u\n",
				(uint32_t)(cons->expression_num * sizeof(policy_expression_t)));
		return -ENOMEM;
	}

	for (i = 0; i < cons->expression_num; ++i) {
		rc = pp_read_expr(fp, &(cons->expression_list[i]));
		if (rc) {
			goto pp_read_cons_fail;
		}
	}

	return 0;

pp_read_cons_fail:

	if (cons->expression_list) {
		pp_free(cons->expression_list);
		cons->expression_list = NULL;
	}

	return rc;
}

static int pp_read_expanded_range(struct policy_file *fp, policy_exp_range_t *range)
{
	int rc;

	rc = next_entry(&(range->sens_num), fp, sizeof(range->sens_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(range->sens_low), fp, sizeof(range->sens_low));
	if (rc) {
		return rc;
	}

	if (le32_to_cpu(range->sens_num) > 1) {
		rc = next_entry(&(range->sens_high), fp, sizeof(range->sens_high));
		if (rc) {
			return rc;
		}
	}

	rc = pp_read_bitmap(fp, &(range->cat_low));
	if (rc) {
		return rc;
	}

	if (le32_to_cpu(range->sens_num) > 1) {
		rc = pp_read_bitmap(fp, &(range->cat_high));
		if (rc) {
			return rc;
		}
	}

	return 0;
}

static int pp_read_expanded_dfltlevel(struct policy_file *fp, policy_exp_dfltlevel_t *level)
{
	int rc;

	rc = next_entry(&(level->sens), fp, sizeof(level->sens));
	if (rc) {
		return rc;
	}

	rc = pp_read_bitmap(fp, &(level->cat));
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_extended_perms(struct policy_file *fp, policy_avtab_extended_perms_t *avtab_xperms)
{
	int rc;

	rc = next_entry(&(avtab_xperms->specified), fp, sizeof(avtab_xperms->specified));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(avtab_xperms->driver), fp, sizeof(avtab_xperms->driver));
	if (rc) {
		return rc;
	}

	rc = next_entry(avtab_xperms->perms, fp, sizeof(avtab_xperms->perms[0]) * PP_PERMS_LEN);
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_context(struct policy_file *fp, policy_context_t *con)
{
	int rc;

	rc = next_entry(&(con->user), fp, sizeof(con->user));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(con->role), fp, sizeof(con->role));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(con->ctype), fp, sizeof(con->ctype));
	if (rc) {
		return rc;
	}

	rc = pp_read_expanded_range(fp, &(con->exp_range));
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_common_helper(struct policy_file *fp, policy_common_t *common)
{
	uint32_t i;
	int rc;

	rc = next_entry(&(common->name_len), fp, sizeof(common->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(common->datums), fp, sizeof(common->datums));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(common->primary_name_num), fp, sizeof(common->primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(common->element_num), fp, sizeof(common->element_num));
	if (rc) {
		return rc;
	}

	if (common->name_len) {
		common->name = pp_zalloc(common->name_len + 1);
		if (!common->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", common->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(common->name, fp, common->name_len);
		if (rc) {
			goto pp_read_common_helper_fail;
		}
	}

	if (common->element_num) {
		common->perm_list = pp_zalloc(common->element_num * sizeof(policy_perm_t));
		if (!common->perm_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"permission list of length %u\n",
					(uint32_t)(common->element_num * sizeof(policy_perm_t)));
			rc = -ENOMEM;
			goto pp_read_common_helper_fail;
		}

		for (i = 0; i < common->element_num; ++i) {
			rc = pp_read_perm(fp, &(common->perm_list[i]));
			if (rc) {
				goto pp_read_common_helper_fail;
			}
		}
	}

	return 0;

pp_read_common_helper_fail:

	if (common->perm_list) {
		pp_free(common->perm_list);
		common->perm_list = NULL;
	}

	if (common->name) {
		pp_free(common->name);
		common->name = NULL;
	}

	return rc;
}

static int pp_read_common(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_common_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_common.primary_name_num), fp, sizeof(db->symtab_common.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_common.element_num), fp, sizeof(db->symtab_common.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_common.element_num) {
		return 0;
	}

	db->symtab_common.attr_list = pp_zalloc(db->symtab_common.element_num * sizeof(policy_common_t));
	if (!db->symtab_common.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_common.element_num * sizeof(policy_common_t)));
		return -ENOMEM;
	}
	list = (policy_common_t *)db->symtab_common.attr_list;

	for (i = 0; i < db->symtab_common.element_num; ++i) {
		rc = pp_read_common_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_common_fail;
		}
	}

	return 0;

pp_read_common_fail:

	if (db->symtab_common.attr_list) {
		pp_free(db->symtab_common.attr_list);
		db->symtab_common.attr_list = NULL;
	}

	return rc;
}

static int pp_read_classes_helper(struct policy_file *fp, policy_classes_t *classes)
{
	uint32_t i;
	int rc;

	rc = next_entry(&(classes->name_len), fp, sizeof(classes->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(classes->common_name_len), fp, sizeof(classes->common_name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(classes->datums), fp, sizeof(classes->datums));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(classes->primary_name_num), fp, sizeof(classes->primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(classes->element_num), fp, sizeof(classes->element_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(classes->constraints_num), fp, sizeof(classes->constraints_num));
	if (rc) {
		return rc;
	}

	if (classes->name_len) {
		classes->name = pp_zalloc(classes->name_len + 1);
		if (!classes->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", classes->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(classes->name, fp, classes->name_len);
		if (rc) {
			goto pp_read_classes_helper_fail;
		}
	}

	if (classes->common_name_len) {
		classes->common_name = pp_zalloc(classes->common_name_len + 1);
		if (!classes->common_name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", classes->common_name_len + 1);
			rc = -ENOMEM;
			goto pp_read_classes_helper_fail;
		}

		rc = next_entry(classes->common_name, fp, classes->common_name_len);
		if (rc) {
			goto pp_read_classes_helper_fail;
		}
	}

	if (classes->element_num) {
		classes->perm_list = pp_zalloc(classes->element_num * sizeof(policy_perm_t));
		if (!classes->perm_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"permission list of length %u\n",
					(uint32_t)(classes->element_num * sizeof(policy_perm_t)));
			rc = -ENOMEM;
			goto pp_read_classes_helper_fail;
		}

		for (i = 0; i < classes->element_num; ++i) {
			rc = pp_read_perm(fp, &(classes->perm_list[i]));
			if (rc) {
				goto pp_read_classes_helper_fail;
			}
		}
	}

	if (classes->constraints_num) {
		classes->constraints_list = pp_zalloc(classes->constraints_num * sizeof(policy_constraints_t));
		if (!classes->constraints_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"constraints list of length %u\n",
					(uint32_t)(classes->constraints_num * sizeof(policy_constraints_t)));
			rc = -ENOMEM;
			goto pp_read_classes_helper_fail;
		}

		for (i = 0; i < classes->constraints_num; ++i) {
			rc = pp_read_cons(fp, &(classes->constraints_list[i]));
			if (rc) {
				goto pp_read_classes_helper_fail;
			}
		}
	}

	rc = next_entry(&(classes->validatetrans_num), fp, sizeof(classes->validatetrans_num));
	if (rc) {
		goto pp_read_classes_helper_fail;
	}

	if (classes->validatetrans_num) {
		classes->validatetrans_list = pp_zalloc(classes->validatetrans_num * sizeof(policy_constraints_t));
		if (!classes->validatetrans_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"validatetrans list of length %u\n",
					(uint32_t)(classes->validatetrans_num * sizeof(policy_constraints_t)));
			rc = -ENOMEM;
			goto pp_read_classes_helper_fail;
		}

		for (i = 0; i < classes->validatetrans_num; ++i) {
			rc = pp_read_cons(fp, &(classes->validatetrans_list[i]));
			if (rc) {
				goto pp_read_classes_helper_fail;
			}
		}
	}

	if (pp_version_num >= POLICYDB_VERSION_NEW_OBJECT_DEFAULTS) {
		rc = next_entry(&(classes->default_user), fp, sizeof(classes->default_user));
		if (rc) {
			goto pp_read_classes_helper_fail;
		}

		rc = next_entry(&(classes->default_role), fp, sizeof(classes->default_role));
		if (rc) {
			goto pp_read_classes_helper_fail;
		}

		rc = next_entry(&(classes->default_range), fp, sizeof(classes->default_range));
		if (rc) {
			goto pp_read_classes_helper_fail;
		}
	}

	if (pp_version_num >= POLICYDB_VERSION_DEFAULT_TYPE) {
		rc = next_entry(&(classes->default_type), fp, sizeof(classes->default_type));
		if (rc) {
			goto pp_read_classes_helper_fail;
		}
	}

	return 0;

pp_read_classes_helper_fail:

	if (classes->validatetrans_list) {
		pp_free(classes->validatetrans_list);
		classes->validatetrans_list = NULL;
	}

	if (classes->constraints_list) {
		pp_free(classes->constraints_list);
		classes->constraints_list = NULL;
	}

	if (classes->perm_list) {
		pp_free(classes->perm_list);
		classes->perm_list = NULL;
	}

	if (classes->common_name) {
		pp_free(classes->common_name);
		classes->common_name = NULL;
	}

	if (classes->name) {
		pp_free(classes->name);
		classes->name = NULL;
	}

	return rc;
}

static int pp_read_classes(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_classes_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_classes.primary_name_num), fp, sizeof(db->symtab_classes.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_classes.element_num), fp, sizeof(db->symtab_classes.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_classes.element_num) {
		return 0;
	}

	db->symtab_classes.attr_list = pp_zalloc(db->symtab_classes.element_num * sizeof(policy_classes_t));
	if (!db->symtab_classes.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_classes.element_num * sizeof(policy_classes_t)));
		return -ENOMEM;
	}
	list = (policy_classes_t *)db->symtab_classes.attr_list;

	for (i = 0; i < db->symtab_classes.element_num; ++i) {
		rc = pp_read_classes_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_classes_fail;
		}
	}

	return 0;

pp_read_classes_fail:

	if (db->symtab_classes.attr_list) {
		pp_free(db->symtab_classes.attr_list);
		db->symtab_classes.attr_list = NULL;
	}

	return rc;
}

static int pp_read_roles_helper(struct policy_file *fp, policy_roles_t *roles)
{
	int rc;

	rc = next_entry(&(roles->name_len), fp, sizeof(roles->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(roles->datums), fp, sizeof(roles->datums));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(roles->bounds), fp, sizeof(roles->bounds));
	if (rc) {
		return rc;
	}

	if (roles->name_len) {
		roles->name = pp_zalloc(roles->name_len + 1);
		if (!roles->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", roles->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(roles->name, fp, roles->name_len);
		if (rc) {
			goto pp_read_roles_helper_fail;
		}
	}

	rc = pp_read_bitmap(fp, &(roles->dominates));
	if (rc) {
		goto pp_read_roles_helper_fail;
	}

	/*
	* Write an empty ebitmap to make CIL and kernel consistent in case of role->s.value == OBJECT_R_VAL
	* See: external/selinux/libsepol/src/write.c: line 1095
	*/
	rc = pp_read_bitmap(fp, &(roles->types));
	if (rc) {
		goto pp_read_roles_helper_fail;
	}

	return 0;

pp_read_roles_helper_fail:

	if (roles->name) {
		pp_free(roles->name);
		roles->name = NULL;
	}

	return rc;
}

static int pp_read_roles(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_roles_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_roles.primary_name_num), fp, sizeof(db->symtab_roles.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_roles.element_num), fp, sizeof(db->symtab_roles.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_roles.element_num) {
		return 0;
	}

	db->symtab_roles.attr_list = pp_zalloc(db->symtab_roles.element_num * sizeof(policy_roles_t));
	if (!db->symtab_roles.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_roles.element_num * sizeof(policy_roles_t)));
		return -ENOMEM;
	}
	list = (policy_roles_t *)db->symtab_roles.attr_list;

	for (i = 0; i < db->symtab_roles.element_num; ++i) {
		rc = pp_read_roles_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_roles_fail;
		}
	}

	return 0;

pp_read_roles_fail:

	if (db->symtab_roles.attr_list) {
		pp_free(db->symtab_roles.attr_list);
		db->symtab_roles.attr_list = NULL;
	}

	return rc;
}

static int pp_read_types_helper(struct policy_file *fp, policy_types_t *types)
{
	int rc;

	rc = next_entry(&(types->name_len), fp, sizeof(types->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(types->datums), fp, sizeof(types->datums));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(types->properties), fp, sizeof(types->properties));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(types->bounds), fp, sizeof(types->bounds));
	if (rc) {
		return rc;
	}

	if (!types->name_len) {
		return 0;
	}

	types->name = pp_zalloc(types->name_len + 1);
	if (!types->name) {
		pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", types->name_len + 1);
		return -ENOMEM;
	}

	rc = next_entry(types->name, fp, types->name_len);
	if (rc) {
		goto pp_read_types_helper_fail;
	}

	return 0;

pp_read_types_helper_fail:

	if (types->name) {
		pp_free(types->name);
		types->name = NULL;
	}

	return rc;
}

static int pp_read_types(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_types_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_types.primary_name_num), fp, sizeof(db->symtab_types.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_types.element_num), fp, sizeof(db->symtab_types.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_types.element_num) {
		return 0;
	}

	db->symtab_types.attr_list = pp_zalloc(db->symtab_types.element_num * sizeof(policy_types_t));
	if (!db->symtab_types.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_types.element_num * sizeof(policy_types_t)));
		return -ENOMEM;
	}
	list = (policy_types_t *)db->symtab_types.attr_list;

	for (i = 0; i < db->symtab_types.element_num; ++i) {
		rc = pp_read_types_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_types_fail;
		}
	}

	return 0;

pp_read_types_fail:

	if (db->symtab_types.attr_list) {
		pp_free(db->symtab_types.attr_list);
		db->symtab_types.attr_list = NULL;
	}

	return rc;
}

static int pp_read_users_helper(struct policy_file *fp, policy_users_t *users)
{
	int rc;

	rc = next_entry(&(users->name_len), fp, sizeof(users->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(users->datums), fp, sizeof(users->datums));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(users->bounds), fp, sizeof(users->bounds));
	if (rc) {
		return rc;
	}

	if (users->name_len) {
		users->name = pp_zalloc(users->name_len + 1);
		if (!users->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", users->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(users->name, fp, users->name_len);
		if (rc) {
			goto pp_read_users_helper_fail;
		}
	}

	rc = pp_read_bitmap(fp, &(users->roles));
	if (rc) {
		goto pp_read_users_helper_fail;
	}

	rc = pp_read_expanded_range(fp, &(users->exp_range));
	if (rc) {
		goto pp_read_users_helper_fail;
	}

	rc = pp_read_expanded_dfltlevel(fp, &(users->exp_dfltlevel));
	if (rc) {
		goto pp_read_users_helper_fail;
	}

	return 0;

pp_read_users_helper_fail:

	if (users->name) {
		pp_free(users->name);
		users->name = NULL;
	}

	return rc;
}

static int pp_read_users(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_users_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_users.primary_name_num), fp, sizeof(db->symtab_users.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_users.element_num), fp, sizeof(db->symtab_users.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_users.element_num) {
		return 0;
	}

	db->symtab_users.attr_list = pp_zalloc(db->symtab_users.element_num * sizeof(policy_users_t));
	if (!db->symtab_users.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_users.element_num * sizeof(policy_users_t)));
		return -ENOMEM;
	}
	list = (policy_users_t *)db->symtab_users.attr_list;

	for (i = 0; i < db->symtab_users.element_num; ++i) {
		rc = pp_read_users_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_users_fail;
		}
	}

	return 0;

pp_read_users_fail:

	if (db->symtab_users.attr_list) {
		pp_free(db->symtab_users.attr_list);
		db->symtab_users.attr_list = NULL;
	}

	return rc;
}

static int pp_read_bools_helper(struct policy_file *fp, policy_bools_t *bools)
{
	int rc;

	rc = next_entry(&(bools->datums), fp, sizeof(bools->datums));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(bools->state), fp, sizeof(bools->state));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(bools->name_len), fp, sizeof(bools->name_len));
	if (rc) {
		return rc;
	}

	if (!bools->name_len) {
		return 0;
	}

	bools->name = pp_zalloc(bools->name_len + 1);
	if (!bools->name) {
		pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", bools->name_len + 1);
		return -ENOMEM;
	}

	rc = next_entry(bools->name, fp, bools->name_len);
	if (rc) {
		goto pp_read_bools_helper_fail;
	}

	return 0;

pp_read_bools_helper_fail:

	if (bools->name) {
		pp_free(bools->name);
		bools->name = NULL;
	}

	return rc;
}

static int pp_read_bools(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_bools_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_bools.primary_name_num), fp, sizeof(db->symtab_bools.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_bools.element_num), fp, sizeof(db->symtab_bools.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_bools.element_num) {
		return 0;
	}

	db->symtab_bools.attr_list = pp_zalloc(db->symtab_bools.element_num * sizeof(policy_bools_t));
	if (!db->symtab_bools.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_bools.element_num * sizeof(policy_bools_t)));
		return -ENOMEM;
	}
	list = (policy_bools_t *)db->symtab_bools.attr_list;

	for (i = 0; i < db->symtab_bools.element_num; ++i) {
		rc = pp_read_bools_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_bools_fail;
		}
	}

	return 0;

pp_read_bools_fail:

	if (db->symtab_bools.attr_list) {
		pp_free(db->symtab_bools.attr_list);
		db->symtab_bools.attr_list = NULL;
	}

	return rc;
}

static int pp_read_levels_helper(struct policy_file *fp, policy_levels_t *levels)
{
	int rc;

	rc = next_entry(&(levels->name_len), fp, sizeof(levels->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(levels->isalias), fp, sizeof(levels->isalias));
	if (rc) {
		return rc;
	}

	if (levels->name_len) {
		levels->name = pp_zalloc(levels->name_len + 1);
		if (!levels->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", levels->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(levels->name, fp, levels->name_len);
		if (rc) {
			goto pp_read_levels_helper_fail;
		}
	}

	rc = pp_read_expanded_dfltlevel(fp, &(levels->level));
	if (rc) {
		goto pp_read_levels_helper_fail;
	}

	return 0;

pp_read_levels_helper_fail:

	if (levels->name) {
		pp_free(levels->name);
		levels->name = NULL;
	}

	return rc;
}

static int pp_read_levels(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_levels_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_levels.primary_name_num), fp, sizeof(db->symtab_levels.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_levels.element_num), fp, sizeof(db->symtab_levels.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_levels.element_num) {
		return 0;
	}

	db->symtab_levels.attr_list = pp_zalloc(db->symtab_levels.element_num * sizeof(policy_levels_t));
	if (!db->symtab_levels.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_levels.element_num * sizeof(policy_levels_t)));
		return -ENOMEM;
	}
	list = (policy_levels_t *)db->symtab_levels.attr_list;

	for (i = 0; i < db->symtab_levels.element_num; ++i) {
		rc = pp_read_levels_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_levels_fail;
		}
	}

	return 0;

pp_read_levels_fail:

	if (db->symtab_levels.attr_list) {
		pp_free(db->symtab_levels.attr_list);
		db->symtab_levels.attr_list = NULL;
	}

	return rc;
}

static int pp_read_cats_helper(struct policy_file *fp, policy_cats_t *cats)
{
	int rc;

	rc = next_entry(&(cats->name_len), fp, sizeof(cats->name_len));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(cats->datums), fp, sizeof(cats->datums));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(cats->isalias), fp, sizeof(cats->isalias));
	if (rc) {
		return rc;
	}

	if (!cats->name_len) {
		return 0;
	}

	cats->name = pp_zalloc(cats->name_len + 1);
	if (!cats->name) {
		pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", cats->name_len + 1);
		return -ENOMEM;
	}

	rc = next_entry(cats->name, fp, cats->name_len);
	if (rc) {
		goto pp_read_cats_helper_fail;
	}

	return 0;

pp_read_cats_helper_fail:

	if (cats->name) {
		pp_free(cats->name);
		cats->name = NULL;
	}

	return rc;
}

static int pp_read_cats(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_cats_t *list = NULL;
	int rc;

	rc = next_entry(&(db->symtab_cats.primary_name_num), fp, sizeof(db->symtab_cats.primary_name_num));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(db->symtab_cats.element_num), fp, sizeof(db->symtab_cats.element_num));
	if (rc) {
		return rc;
	}

	if (!db->symtab_cats.element_num) {
		return 0;
	}

	db->symtab_cats.attr_list = pp_zalloc(db->symtab_cats.element_num * sizeof(policy_cats_t));
	if (!db->symtab_cats.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->symtab_cats.element_num * sizeof(policy_cats_t)));
		return -ENOMEM;
	}
	list = (policy_cats_t *)db->symtab_cats.attr_list;

	for (i = 0; i < db->symtab_cats.element_num; ++i) {
		rc = pp_read_cats_helper(fp, &list[i]);
		if (rc) {
			goto pp_read_cats_fail;
		}
	}

	return 0;

pp_read_cats_fail:

	if (db->symtab_cats.attr_list) {
		pp_free(db->symtab_cats.attr_list);
		db->symtab_cats.attr_list = NULL;
	}

	return rc;
}

static int pp_read_avtab_item(struct policy_file *fp, policy_avtab_item_t *item)
{
	int rc;

	rc = next_entry(&(item->source_type), fp, sizeof(item->source_type));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->target_type), fp, sizeof(item->target_type));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->target_class), fp, sizeof(item->target_class));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->specified), fp, sizeof(item->specified));
	if (rc) {
		return rc;
	}

	if (item->specified & AVTAB_XPERMS) {
		rc = pp_read_extended_perms(fp, &(item->extended_perms));
		if (rc) {
			return rc;
		}
	} else {
		rc = next_entry(&(item->datums), fp, sizeof(item->datums));
		if (rc) {
			return rc;
		}
	}

	return 0;
}

static int pp_read_cond_expr(struct policy_file *fp, policy_cond_expr_t *expr)
{
	int rc;

	rc = next_entry(&(expr->expr_type), fp, sizeof(expr->expr_type));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(expr->boolean), fp, sizeof(expr->boolean));
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_condlist_item(struct policy_file *fp, policy_condlist_item_t *item)
{
	uint32_t i;
	int rc;

	rc = next_entry(&(item->cur_state), fp, sizeof(item->cur_state));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->expression_num), fp, sizeof(item->expression_num));
	if (rc) {
		return rc;
	}

	if (item->expression_num) {
		item->expression_list = pp_zalloc(item->expression_num * sizeof(policy_cond_expr_t));
		if (!item->expression_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"expression list of length %u\n",
					(uint32_t)(item->expression_num * sizeof(policy_cond_expr_t)));
			return -ENOMEM;
		}

		for (i = 0; i < item->expression_num; ++i) {
			rc = pp_read_cond_expr(fp, &(item->expression_list[i]));
			if (rc) {
				goto pp_read_condlist_item_fail;
			}
		}
	}

	rc = next_entry(&(item->true_list.element_num), fp, sizeof(item->true_list.element_num));
	if (rc) {
		goto pp_read_condlist_item_fail;
	}

	if (item->true_list.element_num) {
		item->true_list.av_list = pp_zalloc(item->true_list.element_num * sizeof(policy_avtab_item_t));
		if (!item->true_list.av_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"true list of length %u\n",
					(uint32_t)(item->true_list.element_num * sizeof(policy_avtab_item_t)));
			rc = -ENOMEM;
			goto pp_read_condlist_item_fail;
		}

		for (i = 0; i < item->true_list.element_num; ++i) {
			rc = pp_read_avtab_item(fp, &(item->true_list.av_list[i]));
			if (rc) {
				goto pp_read_condlist_item_fail;
			}
		}
	}

	rc = next_entry(&(item->false_list.element_num), fp, sizeof(item->false_list.element_num));
	if (rc) {
		goto pp_read_condlist_item_fail;
	}

	if (item->false_list.element_num) {
		item->false_list.av_list = pp_zalloc(item->false_list.element_num * sizeof(policy_avtab_item_t));
		if (!item->false_list.av_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"false list of length %u\n",
					(uint32_t)(item->false_list.element_num * sizeof(policy_avtab_item_t)));
			rc = -ENOMEM;
			goto pp_read_condlist_item_fail;
		}

		for (i = 0; i < item->false_list.element_num; ++i) {
			rc = pp_read_avtab_item(fp, &(item->false_list.av_list[i]));
			if (rc) {
				goto pp_read_condlist_item_fail;
			}
		}
	}

	return 0;

pp_read_condlist_item_fail:

	if (item->false_list.av_list) {
		pp_free(item->false_list.av_list);
		item->false_list.av_list = NULL;
	}

	if (item->true_list.av_list) {
		pp_free(item->true_list.av_list);
		item->true_list.av_list = NULL;
	}

	if (item->expression_list) {
		pp_free(item->expression_list);
		item->expression_list = NULL;
	}

	return rc;
}

static int pp_read_roletrans_item(struct policy_file *fp, policy_roletrans_item_t *item)
{
	int rc;

	rc = next_entry(&(item->role), fp, sizeof(item->role));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->role_type), fp, sizeof(item->role_type));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->new_role), fp, sizeof(item->new_role));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->tclass), fp, sizeof(item->tclass));
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_roleallow_item(struct policy_file *fp, policy_roleallow_item_t *item)
{
	int rc;

	rc = next_entry(&(item->role), fp, sizeof(item->role));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->new_role), fp, sizeof(item->new_role));
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_filenametrans_item(struct policy_file *fp, policy_filenametrans_item_t *item)
{
	int rc;

	rc = next_entry(&(item->name_len), fp, sizeof(item->name_len));
	if (rc) {
		return rc;
	}

	if (item->name_len) {
		item->name = pp_zalloc(item->name_len + 1);
		if (!item->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", item->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(item->name, fp, item->name_len);
		if (rc) {
			goto pp_read_filenametrans_item_fail;
		}
	}

	rc = next_entry(&(item->stype), fp, sizeof(item->stype));
	if (rc) {
		goto pp_read_filenametrans_item_fail;
	}

	rc = next_entry(&(item->ttype), fp, sizeof(item->ttype));
	if (rc) {
		goto pp_read_filenametrans_item_fail;
	}

	rc = next_entry(&(item->tclass), fp, sizeof(item->tclass));
	if (rc) {
		goto pp_read_filenametrans_item_fail;
	}

	rc = next_entry(&(item->otype), fp, sizeof(item->otype));
	if (rc) {
		goto pp_read_filenametrans_item_fail;
	}

	return 0;

pp_read_filenametrans_item_fail:

	if (item->name) {
		pp_free(item->name);
		item->name = NULL;
	}

	return rc;
}

static int pp_read_ocon_isid(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_isid_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->isid_num), fp, sizeof(ocon->isid_num));
	if (rc) {
		return rc;
	}

	if (!ocon->isid_num) {
		return 0;
	}

	ocon->isid_list = pp_zalloc(ocon->isid_num * sizeof(policy_ocon_isid_t));
	if (!ocon->isid_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"isid list of length %u\n",
				(uint32_t)(ocon->isid_num * sizeof(policy_ocon_isid_t)));
		return -ENOMEM;
	}
	list = ocon->isid_list;

	for (i = 0; i < ocon->isid_num; ++i) {
		rc = next_entry(&(list[i].sid), fp, sizeof(list[i].sid));
		if (rc) {
			goto pp_read_ocon_isid_fail;
		}

		rc = pp_read_context(fp, &(list[i].con));
		if (rc) {
			goto pp_read_ocon_isid_fail;
		}
	}

	return 0;

pp_read_ocon_isid_fail:

	if (ocon->isid_list) {
		pp_free(ocon->isid_list);
		ocon->isid_list = NULL;
	}

	return rc;
}

static int pp_read_ocon_fs(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_fs_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->fs_num), fp, sizeof(ocon->fs_num));
	if (rc) {
		return rc;
	}

	if (!ocon->fs_num) {
		return 0;
	}

	ocon->fs_list = pp_zalloc(ocon->fs_num * sizeof(policy_ocon_fs_t));
	if (!ocon->fs_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
					"fs list of length %u\n", (uint32_t)(ocon->fs_num * sizeof(policy_ocon_fs_t)));
		return -ENOMEM;
	}
	list = ocon->fs_list;

	for (i = 0; i < ocon->fs_num; ++i) {
		rc = next_entry(&(list[i].name_len), fp, sizeof(list[i].name_len));
		if (rc) {
			goto pp_read_ocon_fs_fail;
		}

		if (list[i].name_len) {
			list[i].name = pp_zalloc(list[i].name_len + 1);
			if (!list[i].name) {
				pr_err("SELinux: unable to allocate memory for policydb "
							"string of length %d\n", list[i].name_len + 1);
				goto pp_read_ocon_fs_fail;
			}

			rc = next_entry(list[i].name, fp, list[i].name_len);
			if (rc) {
				goto pp_read_ocon_fs_fail;
			}
		}

		rc = pp_read_context(fp, &(list[i].con_0));
		if (rc) {
			goto pp_read_ocon_fs_fail;
		}

		rc = pp_read_context(fp, &(list[i].con_1));
		if (rc) {
			goto pp_read_ocon_fs_fail;
		}
	}

	return 0;

pp_read_ocon_fs_fail:

	for (i = 0; i < ocon->fs_num; ++i) {
		if (list[i].name) {
			pp_free(list[i].name);
			list[i].name = NULL;
		}
	}

	if (ocon->fs_list) {
		pp_free(ocon->fs_list);
		ocon->fs_list = NULL;
	}

	return rc;
}

static int pp_read_ocon_port(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_port_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->port_num), fp, sizeof(ocon->port_num));
	if (rc) {
		return rc;
	}

	if (!ocon->port_num) {
		return 0;
	}

	ocon->port_list = pp_zalloc(ocon->port_num * sizeof(policy_ocon_port_t));
	if (!ocon->port_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"port list of length %u\n",
				(uint32_t)(ocon->port_num * sizeof(policy_ocon_port_t)));
		return -ENOMEM;
	}
	list = ocon->port_list;

	for (i = 0; i < ocon->port_num; ++i) {
		rc = next_entry(&(list[i].protocol), fp, sizeof(list[i].protocol));
		if (rc) {
			goto pp_read_ocon_port_fail;
		}

		rc = next_entry(&(list[i].low_port), fp, sizeof(list[i].low_port));
		if (rc) {
			goto pp_read_ocon_port_fail;
		}

		rc = next_entry(&(list[i].high_port), fp, sizeof(list[i].high_port));
		if (rc) {
			goto pp_read_ocon_port_fail;
		}

		rc = pp_read_context(fp, &(list[i].con));
		if (rc) {
			goto pp_read_ocon_port_fail;
		}
	}

	return 0;

pp_read_ocon_port_fail:

	if (ocon->port_list) {
		pp_free(ocon->port_list);
		ocon->port_list = NULL;
	}

	return rc;
}

static int pp_read_ocon_netif(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_netif_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->netif_num), fp, sizeof(ocon->netif_num));
	if (rc) {
		return rc;
	}

	if (!ocon->netif_num) {
		return 0;
	}

	ocon->netif_list = pp_zalloc(ocon->netif_num * sizeof(policy_ocon_netif_t));
	if (!ocon->netif_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"netif list of length %u\n",
				(uint32_t)(ocon->netif_num * sizeof(policy_ocon_netif_t)));
		return -ENOMEM;
	}
	list = ocon->netif_list;

	for (i = 0; i < ocon->netif_num; ++i) {
		rc = next_entry(&(list[i].name_len), fp, sizeof(list[i].name_len));
		if (rc) {
			goto pp_read_ocon_netif_fail;
		}

		if (list[i].name_len) {
			list[i].name = pp_zalloc(list[i].name_len + 1);
			if (!list[i].name) {
				pr_err("SELinux: unable to allocate memory for policydb "
							"string of length %d\n", list[i].name_len + 1);
				goto pp_read_ocon_netif_fail;
			}

			rc = next_entry(list[i].name, fp, list[i].name_len);
			if (rc) {
				goto pp_read_ocon_netif_fail;
			}
		}

		rc = pp_read_context(fp, &(list[i].con_0));
		if (rc) {
			goto pp_read_ocon_netif_fail;
		}

		rc = pp_read_context(fp, &(list[i].con_1));
		if (rc) {
			goto pp_read_ocon_netif_fail;
		}
	}

	return 0;

pp_read_ocon_netif_fail:

	for (i = 0; i < ocon->netif_num; ++i) {
		if (list[i].name) {
			pp_free(list[i].name);
			list[i].name = NULL;
		}
	}

	if (ocon->netif_list) {
		pp_free(ocon->netif_list);
		ocon->netif_list = NULL;
	}

	return rc;
}

static int pp_read_ocon_node(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_node_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->node_num), fp, sizeof(ocon->node_num));
	if (rc) {
		return rc;
	}

	if (!ocon->node_num) {
		return 0;
	}

	ocon->node_list = pp_zalloc(ocon->node_num * sizeof(policy_ocon_node_t));
	if (!ocon->node_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"node list of length %u\n",
				(uint32_t)(ocon->node_num * sizeof(policy_ocon_node_t)));
		return -ENOMEM;
	}
	list = ocon->node_list;

	for (i = 0; i < ocon->node_num; ++i) {
		rc = next_entry(&(list[i].addr), fp, sizeof(list[i].addr));
		if (rc) {
			goto pp_read_ocon_node_fail;
		}

		rc = next_entry(&(list[i].mask), fp, sizeof(list[i].mask));
		if (rc) {
			goto pp_read_ocon_node_fail;
		}

		rc = pp_read_context(fp, &(list[i].con));
		if (rc) {
			goto pp_read_ocon_node_fail;
		}
	}

	return 0;

pp_read_ocon_node_fail:

	if (ocon->node_list) {
		pp_free(ocon->node_list);
		ocon->node_list = NULL;
	}

	return rc;
}

static int pp_read_ocon_fsuse(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_fsuse_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->fsuse_num), fp, sizeof(ocon->fsuse_num));
	if (rc) {
		return rc;
	}

	if (!ocon->fsuse_num) {
		return 0;
	}

	ocon->fsuse_list = pp_zalloc(ocon->fsuse_num * sizeof(policy_ocon_fsuse_t));
	if (!ocon->fsuse_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"fsuse list of length %u\n",
				(uint32_t)(ocon->fsuse_num * sizeof(policy_ocon_fsuse_t)));
		return -ENOMEM;
	}
	list = ocon->fsuse_list;

	for (i = 0; i < ocon->fsuse_num; ++i) {
		rc = next_entry(&(list[i].behavior), fp, sizeof(list[i].behavior));
		if (rc) {
			goto pp_read_ocon_fsuse_fail;
		}

		rc = next_entry(&(list[i].name_len), fp, sizeof(list[i].name_len));
		if (rc) {
			goto pp_read_ocon_fsuse_fail;
		}

		if (list[i].name_len) {
			list[i].name = pp_zalloc(list[i].name_len + 1);
			if (!list[i].name) {
				pr_err("SELinux: unable to allocate memory for policydb "
							"string of length %d\n", list[i].name_len + 1);
				goto pp_read_ocon_fsuse_fail;
			}

			rc = next_entry(list[i].name, fp, list[i].name_len);
			if (rc) {
				goto pp_read_ocon_fsuse_fail;
			}
		}

		rc = pp_read_context(fp, &(list[i].con));
		if (rc) {
			goto pp_read_ocon_fsuse_fail;
		}
	}

	return 0;

pp_read_ocon_fsuse_fail:

	for (i = 0; i < ocon->fsuse_num; ++i) {
		if (list[i].name) {
			pp_free(list[i].name);
			list[i].name = NULL;
		}
	}

	if (ocon->fsuse_list) {
		pp_free(ocon->fsuse_list);
		ocon->fsuse_list = NULL;
	}

	return rc;
}

static int pp_read_ocon_node6(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_node6_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->node6_num), fp, sizeof(ocon->node6_num));
	if (rc) {
		return rc;
	}

	if (!ocon->node6_num) {
		return 0;
	}

	ocon->node6_list = pp_zalloc(ocon->node6_num * sizeof(policy_ocon_node6_t));
	if (!ocon->node_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"node6 list of length %u\n",
				(uint32_t)(ocon->node6_num * sizeof(policy_ocon_node6_t)));
		return -ENOMEM;
	}
	list = ocon->node6_list;

	for (i = 0; i < ocon->node6_num; ++i) {
		rc = next_entry(&(list[i].addr_0), fp, sizeof(list[i].addr_0));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = next_entry(&(list[i].addr_1), fp, sizeof(list[i].addr_1));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = next_entry(&(list[i].addr_2), fp, sizeof(list[i].addr_2));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = next_entry(&(list[i].addr_3), fp, sizeof(list[i].addr_3));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = next_entry(&(list[i].mask_0), fp, sizeof(list[i].mask_0));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = next_entry(&(list[i].mask_1), fp, sizeof(list[i].mask_1));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = next_entry(&(list[i].mask_2), fp, sizeof(list[i].mask_2));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = next_entry(&(list[i].mask_3), fp, sizeof(list[i].mask_3));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}

		rc = pp_read_context(fp, &(list[i].con));
		if (rc) {
			goto pp_read_ocon_node6_fail;
		}
	}

	return 0;

pp_read_ocon_node6_fail:

	if (ocon->node6_list) {
		pp_free(ocon->node6_list);
		ocon->node6_list = NULL;
	}

	return rc;
}

static int __attribute__ ((unused)) pp_read_ocon_ibpkey(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_ibpkey_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->ibpkey_num), fp, sizeof(ocon->ibpkey_num));
	if (rc) {
		return rc;
	}

	if (!ocon->ibpkey_num) {
		return 0;
	}

	ocon->ibpkey_list = pp_zalloc(ocon->ibpkey_num * sizeof(policy_ocon_ibpkey_t));
	if (!ocon->ibpkey_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"ibpkey list of length %u\n",
				(uint32_t)(ocon->ibpkey_num * sizeof(policy_ocon_ibpkey_t)));
		return -ENOMEM;
	}
	list = ocon->ibpkey_list;

	for (i = 0; i < ocon->ibpkey_num; ++i) {
		rc = next_entry(&(list[i].subnet_prefix), fp, sizeof(list[i].subnet_prefix));
		if (rc) {
			goto pp_read_ocon_ibpkey_fail;
		}

		rc = next_entry(&(list[i].low_pkey), fp, sizeof(list[i].low_pkey));
		if (rc) {
			goto pp_read_ocon_ibpkey_fail;
		}

		rc = next_entry(&(list[i].high_pkey), fp, sizeof(list[i].high_pkey));
		if (rc) {
			goto pp_read_ocon_ibpkey_fail;
		}

		rc = pp_read_context(fp, &(list[i].con));
		if (rc) {
			goto pp_read_ocon_ibpkey_fail;
		}
	}

	return 0;

pp_read_ocon_ibpkey_fail:

	if (ocon->ibpkey_list) {
		pp_free(ocon->ibpkey_list);
		ocon->ibpkey_list = NULL;
	}

	return rc;
}

static int __attribute__ ((unused)) pp_read_ocon_ibendport(struct policy_file *fp, policy_ocontext_t *ocon)
{
	uint32_t i;
	policy_ocon_ibendport_t *list = NULL;
	int rc;

	rc = next_entry(&(ocon->ibendport_num), fp, sizeof(ocon->ibendport_num));
	if (rc) {
		return rc;
	}

	if (!ocon->ibendport_num) {
		return 0;
	}

	ocon->ibendport_list = pp_zalloc(ocon->ibendport_num * sizeof(policy_ocon_ibendport_t));
	if (!ocon->ibendport_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"ibendport list of length %u\n",
				(uint32_t)(ocon->ibendport_num * sizeof(policy_ocon_ibendport_t)));
		return -ENOMEM;
	}
	list = ocon->ibendport_list;

	for (i = 0; i < ocon->ibendport_num; ++i) {
		rc = next_entry(&(list[i].dev_name_len), fp, sizeof(list[i].dev_name_len));
		if (rc) {
			goto pp_read_ocon_ibendport_fail;
		}

		rc = next_entry(&(list[i].port), fp, sizeof(list[i].port));
		if (rc) {
			goto pp_read_ocon_ibendport_fail;
		}

		if (list[i].dev_name_len) {
			list[i].dev_name = pp_zalloc(list[i].dev_name_len + 1);
			if (!list[i].dev_name) {
				pr_err("SELinux: unable to allocate memory for policydb "
							"string of length %d\n", list[i].dev_name_len + 1);
				goto pp_read_ocon_ibendport_fail;
			}

			rc = next_entry(list[i].dev_name, fp, list[i].dev_name_len);
			if (rc) {
				goto pp_read_ocon_ibendport_fail;
			}
		}

		rc = pp_read_context(fp, &(list[i].con));
		if (rc) {
			goto pp_read_ocon_ibendport_fail;
		}
	}

	return 0;

pp_read_ocon_ibendport_fail:

	for (i = 0; i < ocon->ibendport_num; ++i) {
		if (list[i].dev_name) {
			pp_free(list[i].dev_name);
			list[i].dev_name = NULL;
		}
	}

	if (ocon->ibendport_list) {
		pp_free(ocon->ibendport_list);
		ocon->ibendport_list = NULL;
	}

	return rc;
}

static int pp_read_genfs_item_helper(struct policy_file *fp, policy_genfs_ocon_item_t *item)
{
	int rc;

	rc = next_entry(&(item->name_len), fp, sizeof(item->name_len));
	if (rc) {
		return rc;
	}

	if (item->name_len) {
		item->name = pp_zalloc(item->name_len + 1);
		if (!item->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
						"string of length %d\n", item->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(item->name, fp, item->name_len);
		if (rc) {
			goto pp_read_genfs_item_helper_fail;
		}
	}

	rc = next_entry(&(item->sclass), fp, sizeof(item->sclass));
	if (rc) {
		goto pp_read_genfs_item_helper_fail;
	}

	rc = pp_read_context(fp, &(item->con));
	if (rc) {
		goto pp_read_genfs_item_helper_fail;
	}

	return 0;

pp_read_genfs_item_helper_fail:

	if (item->name) {
		pp_free(item->name);
		item->name = NULL;
	}

	return rc;
}

static int pp_read_genfs_item(struct policy_file *fp, policy_genfs_item_t *item)
{
	uint32_t i;
	int rc;

	rc = next_entry(&(item->fstype_len), fp, sizeof(item->fstype_len));
	if (rc) {
		return rc;
	}

	if (item->fstype_len) {
		item->fstype = pp_zalloc(item->fstype_len + 1);
		if (!item->fstype) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", item->fstype_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(item->fstype, fp, item->fstype_len);
		if (rc) {
			goto pp_read_genfs_item_fail;
		}
	}

	rc = next_entry(&(item->ocon_num), fp, sizeof(item->ocon_num));
	if (rc) {
		goto pp_read_genfs_item_fail;
	}

	if (item->ocon_num) {
		item->ocon_list = pp_zalloc(item->ocon_num * sizeof(policy_genfs_ocon_item_t));
		if (!item->ocon_list) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"ocontext list of length %u\n",
					(uint32_t)(item->ocon_num * sizeof(policy_genfs_ocon_item_t)));
			goto pp_read_genfs_item_fail;
		}

		for (i = 0; i < item->ocon_num; ++i) {
			rc = pp_read_genfs_item_helper(fp, &(item->ocon_list[i]));
			if (rc) {
				goto pp_read_genfs_item_fail;
			}
		}
	}

	return 0;

pp_read_genfs_item_fail:

	if (item->ocon_list) {
		pp_free(item->ocon_list);
		item->ocon_list = NULL;
	}

	if (item->fstype) {
		pp_free(item->fstype);
		item->fstype = NULL;
	}

	return rc;
}

static int pp_read_range_item(struct policy_file *fp, policy_range_item_t *item)
{
	int rc;

	rc = next_entry(&(item->source_type), fp, sizeof(item->source_type));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->target_type), fp, sizeof(item->target_type));
	if (rc) {
		return rc;
	}

	rc = next_entry(&(item->target_class), fp, sizeof(item->target_class));
	if (rc) {
		return rc;
	}

	rc = pp_read_expanded_range(fp, &(item->target_range));
	if (rc) {
		return rc;
	}

	return 0;
}

static int pp_read_typeattr_map_item(struct policy_file *fp, policy_typeattr_map_item_t *item)
{
	return pp_read_bitmap(fp, &(item->attr_map));
}

static int pp_read_appendix_type(struct policy_file *fp, policy_appendix_types_t *type)
{
	int rc;

	rc = next_entry(&(type->name_len), fp, sizeof(type->name_len));
	if (rc) {
		return rc;
	}

	if (type->name_len) {
		type->name = pp_zalloc(type->name_len + 1);
		if (!type->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", type->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(type->name, fp, type->name_len);
		if (rc) {
			goto pp_read_appendix_type_fail;
		}
	}

	rc = next_entry(&(type->datums), fp, sizeof(type->datums));
	if (rc) {
		goto pp_read_appendix_type_fail;
	}

	return 0;

pp_read_appendix_type_fail:

	if (type->name) {
		pp_free(type->name);
		type->name = NULL;
	}

	return rc;
}

static int pp_read_appendix_perm(struct policy_file *fp, policy_appendix_perms_t *perm)
{
	int rc;

	rc = next_entry(&(perm->name_len), fp, sizeof(perm->name_len));
	if (rc) {
		return rc;
	}

	if (perm->name_len) {
		perm->name = pp_zalloc(perm->name_len + 1);
		if (!perm->name) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", perm->name_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(perm->name, fp, perm->name_len);
		if (rc) {
			goto pp_read_appendix_perm_fail;
		}
	}

	rc = next_entry(&(perm->datums), fp, sizeof(perm->datums));
	if (rc) {
		goto pp_read_appendix_perm_fail;
	}

	return 0;

pp_read_appendix_perm_fail:

	if (perm->name) {
		pp_free(perm->name);
		perm->name = NULL;
	}

	return rc;
}

static int pp_read_appendix_class(struct policy_file *fp, policy_appendix_classes_t *class)
{
	uint32_t i;
	policy_appendix_perms_t *list = NULL;
	int rc;

	rc = next_entry(&(class->cname_len), fp, sizeof(class->cname_len));
	if (rc) {
		return rc;
	}

	if (class->cname_len) {
		class->cname = pp_zalloc(class->cname_len + 1);
		if (!class->cname) {
			pr_err("SELinux: unable to allocate memory for policydb "
					"string of length %d\n", class->cname_len + 1);
			return -ENOMEM;
		}

		rc = next_entry(class->cname, fp, class->cname_len);
		if (rc) {
			goto pp_read_appendix_class_fail;
		}
	}

	rc = next_entry(&(class->cdatums), fp, sizeof(class->cdatums));
	if (rc) {
		goto pp_read_appendix_class_fail;
	}

	rc = next_entry(&(class->perms_num), fp, sizeof(class->perms_num));
	if (rc) {
		goto pp_read_appendix_class_fail;
	}

	if (!class->perms_num) {
		rc = -EINVAL;
		goto pp_read_appendix_class_fail;
	}

	class->perms = pp_zalloc(class->perms_num * sizeof(policy_appendix_perms_t));
	if (!class->perms) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(class->perms_num * sizeof(policy_appendix_perms_t)));
		rc = -ENOMEM;
		goto pp_read_appendix_class_fail;
	}
	list = (policy_appendix_perms_t *)class->perms;

	for (i = 0; i < class->perms_num; ++i) {
		rc = pp_read_appendix_perm(fp, &list[i]);
		if (rc) {
			goto pp_read_appendix_class_fail;
		}
	}

	return 0;

pp_read_appendix_class_fail:

	if (class->perms) {
		pp_free(class->perms);
		class->perms = NULL;
	}

	if (class->cname) {
		pp_free(class->cname);
		class->cname = NULL;
	}

	return rc;
}

static int pp_write_bitmap(policy_bitmap_t *map, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(map->map_size), sizeof(map->map_size), 1, fp);
	(void)put_entry(&(map->high_bit), sizeof(map->high_bit), 1, fp);
	(void)put_entry(&(map->node_count), sizeof(map->node_count), 1, fp);

	for (i = 0; i < map->node_count; ++i) {
		(void)put_entry(&(map->node_list[i].start_bit), sizeof(map->node_list[i].start_bit), 1, fp);
		(void)put_entry(&(map->node_list[i].node_map), sizeof(map->node_list[i].node_map), 1, fp);
	}

	return 0;
}

static int pp_write_type_set(policy_type_set_t *type_set, struct policy_file *fp)
{
	(void)pp_write_bitmap(&(type_set->types), fp);
	(void)pp_write_bitmap(&(type_set->negset), fp);
	(void)put_entry(&(type_set->flags), sizeof(type_set->flags), 1, fp);

	return 0;
}

static int pp_write_perm(policy_perm_t *perm, struct policy_file *fp)
{
	(void)put_entry(&(perm->name_len), sizeof(perm->name_len), 1, fp);
	(void)put_entry(&(perm->datums), sizeof(perm->datums), 1, fp);

	if (perm->name_len) {
		(void)put_entry(perm->name, 1, perm->name_len, fp);
	}

	return 0;
}

static int pp_write_expr(policy_expression_t *expr, struct policy_file *fp)
{
	(void)put_entry(&(expr->expr_type), sizeof(expr->expr_type), 1, fp);
	(void)put_entry(&(expr->attr), sizeof(expr->attr), 1, fp);
	(void)put_entry(&(expr->op), sizeof(expr->op), 1, fp);

	if (le32_to_cpu(expr->expr_type) == CEXPR_NAMES) {
		(void)pp_write_bitmap(&(expr->names), fp);

		if (pp_version_num >= POLICYDB_VERSION_CONSTRAINT_NAMES) {
			(void)pp_write_type_set(&(expr->type_names), fp);
		}
	}

	return 0;
}

static int pp_write_cons(policy_constraints_t *cons, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(cons->perms), sizeof(cons->perms), 1, fp);
	(void)put_entry(&(cons->expression_num), sizeof(cons->expression_num), 1, fp);

	for (i = 0; i < cons->expression_num; ++i) {
		(void)pp_write_expr(&(cons->expression_list[i]), fp);
	}

	return 0;
}

static int pp_write_expanded_range(policy_exp_range_t *range, struct policy_file *fp)
{
	(void)put_entry(&(range->sens_num), sizeof(range->sens_num), 1, fp);
	(void)put_entry(&(range->sens_low), sizeof(range->sens_low), 1, fp);

	if (le32_to_cpu(range->sens_num) > 1) {
		(void)put_entry(&(range->sens_high), sizeof(range->sens_high), 1, fp);
	}

	(void)pp_write_bitmap(&(range->cat_low), fp);

	if (le32_to_cpu(range->sens_num) > 1) {
		(void)pp_write_bitmap(&(range->cat_high), fp);
	}

	return 0;
}

static int pp_write_expanded_dfltlevel(policy_exp_dfltlevel_t *level, struct policy_file *fp)
{
	(void)put_entry(&(level->sens), sizeof(level->sens), 1, fp);
	(void)pp_write_bitmap(&(level->cat), fp);

	return 0;
}

static int pp_write_extended_perms(policy_avtab_extended_perms_t *avtab_xperms, struct policy_file *fp)
{
	(void)put_entry(&(avtab_xperms->specified), sizeof(avtab_xperms->specified), 1, fp);
	(void)put_entry(&(avtab_xperms->driver), sizeof(avtab_xperms->driver), 1, fp);
	(void)put_entry(avtab_xperms->perms, sizeof(avtab_xperms->perms[0]), PP_PERMS_LEN, fp);

	return 0;
}

static int pp_write_context(policy_context_t *con, struct policy_file *fp)
{
	(void)put_entry(&(con->user), sizeof(con->user), 1, fp);
	(void)put_entry(&(con->role), sizeof(con->role), 1, fp);
	(void)put_entry(&(con->ctype), sizeof(con->ctype), 1, fp);

	(void)pp_write_expanded_range(&(con->exp_range), fp);

	return 0;
}

static int pp_write_common_helper(policy_common_t *common, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(common->name_len), sizeof(common->name_len), 1, fp);
	(void)put_entry(&(common->datums), sizeof(common->datums), 1, fp);
	(void)put_entry(&(common->primary_name_num), sizeof(common->primary_name_num), 1, fp);
	(void)put_entry(&(common->element_num), sizeof(common->element_num), 1, fp);

	if (common->name_len) {
		(void)put_entry(common->name, 1, common->name_len, fp);
	}

	for (i = 0; i < common->element_num; ++i) {
		(void)pp_write_perm(&(common->perm_list[i]), fp);
	}

	return 0;
}

static int pp_write_common(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_common_t *list = db->symtab_common.attr_list;

	(void)put_entry(&(db->symtab_common.primary_name_num), sizeof(db->symtab_common.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_common.element_num), sizeof(db->symtab_common.element_num), 1, fp);

	for (i = 0; i < db->symtab_common.element_num; ++i) {
		(void)pp_write_common_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_classes_helper(policy_classes_t *classes, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(classes->name_len), sizeof(classes->name_len), 1, fp);
	(void)put_entry(&(classes->common_name_len), sizeof(classes->common_name_len), 1, fp);
	(void)put_entry(&(classes->datums), sizeof(classes->datums), 1, fp);
	(void)put_entry(&(classes->primary_name_num), sizeof(classes->primary_name_num), 1, fp);
	(void)put_entry(&(classes->element_num), sizeof(classes->element_num), 1, fp);
	(void)put_entry(&(classes->constraints_num), sizeof(classes->constraints_num), 1, fp);

	if (classes->name_len) {
		(void)put_entry(classes->name, 1, classes->name_len, fp);
	}

	if (classes->common_name_len) {
		(void)put_entry(classes->common_name, 1, classes->common_name_len, fp);
	}

	for (i = 0; i < classes->element_num; ++i) {
		(void)pp_write_perm(&(classes->perm_list[i]), fp);
	}

	for (i = 0; i < classes->constraints_num; ++i) {
		(void)pp_write_cons(&(classes->constraints_list[i]), fp);
	}

	(void)put_entry(&(classes->validatetrans_num), sizeof(classes->validatetrans_num), 1, fp);

	for (i = 0; i < classes->validatetrans_num; ++i) {
		(void)pp_write_cons(&(classes->validatetrans_list[i]), fp);
	}

	if (pp_version_num >= POLICYDB_VERSION_NEW_OBJECT_DEFAULTS) {
		(void)put_entry(&(classes->default_user), sizeof(classes->default_user), 1, fp);
		(void)put_entry(&(classes->default_role), sizeof(classes->default_role), 1, fp);
		(void)put_entry(&(classes->default_range), sizeof(classes->default_range), 1, fp);
	}

	if (pp_version_num >= POLICYDB_VERSION_DEFAULT_TYPE) {
		(void)put_entry(&(classes->default_type), sizeof(classes->default_type), 1, fp);
	}

	return 0;
}

static int pp_write_classes(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_classes_t *list = db->symtab_classes.attr_list;

	(void)put_entry(&(db->symtab_classes.primary_name_num), sizeof(db->symtab_classes.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_classes.element_num), sizeof(db->symtab_classes.element_num), 1, fp);

	for (i = 0; i < db->symtab_classes.element_num; ++i) {
		(void)pp_write_classes_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_roles_helper(policy_roles_t *roles, struct policy_file *fp)
{
	(void)put_entry(&(roles->name_len), sizeof(roles->name_len), 1, fp);
	(void)put_entry(&(roles->datums), sizeof(roles->datums), 1, fp);
	(void)put_entry(&(roles->bounds), sizeof(roles->bounds), 1, fp);

	if (roles->name_len) {
		(void)put_entry(roles->name, 1, roles->name_len, fp);
	}

	(void)pp_write_bitmap(&(roles->dominates), fp);

	/*
	* Write an empty ebitmap to make CIL and kernel consistent in case of role->s.value == OBJECT_R_VAL
	* See: external/selinux/libsepol/src/write.c: line 1095
	*/
	(void)pp_write_bitmap(&(roles->types), fp);

	return 0;
}

static int pp_write_roles(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_roles_t *list = db->symtab_roles.attr_list;

	(void)put_entry(&(db->symtab_roles.primary_name_num), sizeof(db->symtab_roles.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_roles.element_num), sizeof(db->symtab_roles.element_num), 1, fp);

	for (i = 0; i < db->symtab_roles.element_num; ++i) {
		(void)pp_write_roles_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_types_helper(policy_types_t *types, struct policy_file *fp)
{
	(void)put_entry(&(types->name_len), sizeof(types->name_len), 1, fp);
	(void)put_entry(&(types->datums), sizeof(types->datums), 1, fp);
	(void)put_entry(&(types->properties), sizeof(types->properties), 1, fp);
	(void)put_entry(&(types->bounds), sizeof(types->bounds), 1, fp);

	if (types->name_len) {
		(void)put_entry(types->name, 1, types->name_len, fp);
	}

	return 0;
}

static int pp_write_types(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_types_t *list = db->symtab_types.attr_list;

	(void)put_entry(&(db->symtab_types.primary_name_num), sizeof(db->symtab_types.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_types.element_num), sizeof(db->symtab_types.element_num), 1, fp);

	for (i = 0; i < db->symtab_types.element_num; ++i) {
		(void)pp_write_types_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_users_helper(policy_users_t *users, struct policy_file *fp)
{
	(void)put_entry(&(users->name_len), sizeof(users->name_len), 1, fp);
	(void)put_entry(&(users->datums), sizeof(users->datums), 1, fp);
	(void)put_entry(&(users->bounds), sizeof(users->bounds), 1, fp);

	if (users->name_len) {
		(void)put_entry(users->name, 1, users->name_len, fp);
	}

	(void)pp_write_bitmap(&(users->roles), fp);
	(void)pp_write_expanded_range(&(users->exp_range), fp);
	(void)pp_write_expanded_dfltlevel(&(users->exp_dfltlevel), fp);

	return 0;
}

static int pp_write_users(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_users_t *list = db->symtab_users.attr_list;

	(void)put_entry(&(db->symtab_users.primary_name_num), sizeof(db->symtab_users.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_users.element_num), sizeof(db->symtab_users.element_num), 1, fp);

	for (i = 0; i < db->symtab_users.element_num; ++i) {
		(void)pp_write_users_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_bools_helper(policy_bools_t *bools, struct policy_file *fp)
{
	(void)put_entry(&(bools->datums), sizeof(bools->datums), 1, fp);
	(void)put_entry(&(bools->state), sizeof(bools->state), 1, fp);
	(void)put_entry(&(bools->name_len), sizeof(bools->name_len), 1, fp);

	if (bools->name_len) {
		(void)put_entry(bools->name, 1, bools->name_len, fp);
	}

	return 0;
}

static int pp_write_bools(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_bools_t *list = db->symtab_bools.attr_list;

	(void)put_entry(&(db->symtab_bools.primary_name_num), sizeof(db->symtab_bools.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_bools.element_num), sizeof(db->symtab_bools.element_num), 1, fp);

	for (i = 0; i < db->symtab_bools.element_num; ++i) {
		(void)pp_write_bools_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_levels_helper(policy_levels_t *levels, struct policy_file *fp)
{
	(void)put_entry(&(levels->name_len), sizeof(levels->name_len), 1, fp);
	(void)put_entry(&(levels->isalias), sizeof(levels->isalias), 1, fp);

	if (levels->name_len) {
		(void)put_entry(levels->name, 1, levels->name_len, fp);
	}

	(void)pp_write_expanded_dfltlevel(&(levels->level), fp);

	return 0;
}

static int pp_write_levels(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_levels_t *list = db->symtab_levels.attr_list;

	(void)put_entry(&(db->symtab_levels.primary_name_num), sizeof(db->symtab_levels.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_levels.element_num), sizeof(db->symtab_levels.element_num), 1, fp);

	for (i = 0; i < db->symtab_levels.element_num; ++i) {
		(void)pp_write_levels_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_cats_helper(policy_cats_t *cats, struct policy_file *fp)
{
	(void)put_entry(&(cats->name_len), sizeof(cats->name_len), 1, fp);
	(void)put_entry(&(cats->datums), sizeof(cats->datums), 1, fp);
	(void)put_entry(&(cats->isalias), sizeof(cats->isalias), 1, fp);

	if (cats->name_len) {
		(void)put_entry(cats->name, 1, cats->name_len, fp);
	}

	return 0;
}

static int pp_write_cats(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_cats_t *list = db->symtab_cats.attr_list;

	(void)put_entry(&(db->symtab_cats.primary_name_num), sizeof(db->symtab_cats.primary_name_num), 1, fp);
	(void)put_entry(&(db->symtab_cats.element_num), sizeof(db->symtab_cats.element_num), 1, fp);

	for (i = 0; i < db->symtab_cats.element_num; ++i) {
		(void)pp_write_cats_helper(&(list[i]), fp);
	}

	return 0;
}

static int pp_write_avtab_item(policy_avtab_item_t *item, struct policy_file *fp)
{
	(void)put_entry(&(item->source_type), sizeof(item->source_type), 1, fp);
	(void)put_entry(&(item->target_type), sizeof(item->target_type), 1, fp);
	(void)put_entry(&(item->target_class), sizeof(item->target_class), 1, fp);
	(void)put_entry(&(item->specified), sizeof(item->specified), 1, fp);

	if (item->specified & AVTAB_XPERMS) {
		(void)pp_write_extended_perms(&(item->extended_perms), fp);
	} else {
		(void)put_entry(&(item->datums), sizeof(item->datums), 1, fp);
	}

	return 0;
}

static int pp_write_cond_expr(policy_cond_expr_t *expr, struct policy_file *fp)
{
	(void)put_entry(&(expr->expr_type), sizeof(expr->expr_type), 1, fp);
	(void)put_entry(&(expr->boolean), sizeof(expr->boolean), 1, fp);

	return 0;
}

static int pp_write_condlist_item(policy_condlist_item_t *item, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(item->cur_state), sizeof(item->cur_state), 1, fp);
	(void)put_entry(&(item->expression_num), sizeof(item->expression_num), 1, fp);

	for (i = 0; i < item->expression_num; ++i) {
		(void)pp_write_cond_expr(&(item->expression_list[i]), fp);
	}

	(void)put_entry(&(item->true_list.element_num), sizeof(item->true_list.element_num), 1, fp);

	for (i = 0; i < item->true_list.element_num; ++i) {
		(void)pp_write_avtab_item(&(item->true_list.av_list[i]), fp);
	}

	(void)put_entry(&(item->false_list.element_num), sizeof(item->false_list.element_num), 1, fp);

	for (i = 0; i < item->false_list.element_num; ++i) {
		(void)pp_write_avtab_item(&(item->false_list.av_list[i]), fp);
	}

	return 0;
}

static int pp_write_roletrans_item(policy_roletrans_item_t *item, struct policy_file *fp)
{
	(void)put_entry(&(item->role), sizeof(item->role), 1, fp);
	(void)put_entry(&(item->role_type), sizeof(item->role_type), 1, fp);
	(void)put_entry(&(item->new_role), sizeof(item->new_role), 1, fp);
	(void)put_entry(&(item->tclass), sizeof(item->tclass), 1, fp);

	return 0;
}

static int pp_write_roleallow_item(policy_roleallow_item_t *item, struct policy_file *fp)
{
	(void)put_entry(&(item->role), sizeof(item->role), 1, fp);
	(void)put_entry(&(item->new_role), sizeof(item->new_role), 1, fp);

	return 0;
}

static int pp_write_filenametrans_item(policy_filenametrans_item_t *item, struct policy_file *fp)
{
	(void)put_entry(&(item->name_len), sizeof(item->name_len), 1, fp);

	if (item->name_len) {
		(void)put_entry(item->name, 1, item->name_len, fp);
	}

	(void)put_entry(&(item->stype), sizeof(item->stype), 1, fp);
	(void)put_entry(&(item->ttype), sizeof(item->ttype), 1, fp);
	(void)put_entry(&(item->tclass), sizeof(item->tclass), 1, fp);
	(void)put_entry(&(item->otype), sizeof(item->otype), 1, fp);

	return 0;
}

static int pp_write_ocon_isid(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->isid_num), sizeof(ocon->isid_num), 1, fp);

	for (i = 0; i < ocon->isid_num; ++i) {
		(void)put_entry(&(ocon->isid_list[i].sid), sizeof(ocon->isid_list[i].sid), 1, fp);
		(void)pp_write_context(&(ocon->isid_list[i].con), fp);
	}

	return 0;
}

static int pp_write_ocon_fs(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->fs_num), sizeof(ocon->fs_num), 1, fp);

	for (i = 0; i < ocon->fs_num; ++i) {
		(void)put_entry(&(ocon->fs_list[i].name_len), sizeof(ocon->fs_list[i].name_len), 1, fp);

		if (ocon->fs_list[i].name_len) {
			(void)put_entry(ocon->fs_list[i].name, 1, ocon->fs_list[i].name_len, fp);
		}

		(void)pp_write_context(&(ocon->fs_list[i].con_0), fp);
		(void)pp_write_context(&(ocon->fs_list[i].con_1), fp);
	}

	return 0;
}

static int pp_write_ocon_port(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->port_num), sizeof(ocon->port_num), 1, fp);

	for (i = 0; i < ocon->port_num; ++i) {
		(void)put_entry(&(ocon->port_list[i].protocol), sizeof(ocon->port_list[i].protocol), 1, fp);
		(void)put_entry(&(ocon->port_list[i].low_port), sizeof(ocon->port_list[i].low_port), 1, fp);
		(void)put_entry(&(ocon->port_list[i].high_port), sizeof(ocon->port_list[i].high_port), 1, fp);

		(void)pp_write_context(&(ocon->port_list[i].con), fp);
	}

	return 0;
}

static int pp_write_ocon_netif(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->netif_num), sizeof(ocon->netif_num), 1, fp);

	for (i = 0; i < ocon->netif_num; ++i) {
		(void)put_entry(&(ocon->netif_list[i].name_len), sizeof(ocon->netif_list[i].name_len), 1, fp);

		if (ocon->netif_list[i].name_len) {
			(void)put_entry(ocon->netif_list[i].name, 1, ocon->netif_list[i].name_len, fp);
		}

		(void)pp_write_context(&(ocon->netif_list[i].con_0), fp);
		(void)pp_write_context(&(ocon->netif_list[i].con_1), fp);
	}

	return 0;
}

static int pp_write_ocon_node(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->node_num), sizeof(ocon->node_num), 1, fp);

	for (i = 0; i < ocon->node_num; ++i) {
		(void)put_entry(&(ocon->node_list[i].addr), sizeof(ocon->node_list[i].addr), 1, fp);
		(void)put_entry(&(ocon->node_list[i].mask), sizeof(ocon->node_list[i].mask), 1, fp);

		(void)pp_write_context(&(ocon->node_list[i].con), fp);
	}

	return 0;
}

static int pp_write_ocon_fsuse(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->fsuse_num), sizeof(ocon->fsuse_num), 1, fp);

	for (i = 0; i < ocon->fsuse_num; ++i) {
		(void)put_entry(&(ocon->fsuse_list[i].behavior), sizeof(ocon->fsuse_list[i].behavior), 1, fp);
		(void)put_entry(&(ocon->fsuse_list[i].name_len), sizeof(ocon->fsuse_list[i].name_len), 1, fp);

		if (ocon->fsuse_list[i].name_len) {
			(void)put_entry(ocon->fsuse_list[i].name, 1, ocon->fsuse_list[i].name_len, fp);
		}

		(void)pp_write_context(&(ocon->fsuse_list[i].con), fp);
	}

	return 0;
}

static int pp_write_ocon_node6(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->node6_num), sizeof(ocon->node6_num), 1, fp);

	for (i = 0; i < ocon->node6_num; ++i) {
		(void)put_entry(&(ocon->node6_list[i].addr_0), sizeof(ocon->node6_list[i].addr_0), 1, fp);
		(void)put_entry(&(ocon->node6_list[i].addr_1), sizeof(ocon->node6_list[i].addr_1), 1, fp);
		(void)put_entry(&(ocon->node6_list[i].addr_2), sizeof(ocon->node6_list[i].addr_2), 1, fp);
		(void)put_entry(&(ocon->node6_list[i].addr_3), sizeof(ocon->node6_list[i].addr_3), 1, fp);
		(void)put_entry(&(ocon->node6_list[i].mask_0), sizeof(ocon->node6_list[i].mask_0), 1, fp);
		(void)put_entry(&(ocon->node6_list[i].mask_1), sizeof(ocon->node6_list[i].mask_1), 1, fp);
		(void)put_entry(&(ocon->node6_list[i].mask_2), sizeof(ocon->node6_list[i].mask_2), 1, fp);
		(void)put_entry(&(ocon->node6_list[i].mask_3), sizeof(ocon->node6_list[i].mask_3), 1, fp);

		(void)pp_write_context(&(ocon->node6_list[i].con), fp);
	}

	return 0;
}

static int __attribute__ ((unused)) pp_write_ocon_ibpkey(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->ibpkey_num), sizeof(ocon->ibpkey_num), 1, fp);

	for (i = 0; i < ocon->ibpkey_num; ++i) {
		(void)put_entry(&(ocon->ibpkey_list[i].subnet_prefix), sizeof(ocon->ibpkey_list[i].subnet_prefix),
				1, fp);
		(void)put_entry(&(ocon->ibpkey_list[i].low_pkey), sizeof(ocon->ibpkey_list[i].low_pkey), 1, fp);
		(void)put_entry(&(ocon->ibpkey_list[i].high_pkey), sizeof(ocon->ibpkey_list[i].high_pkey), 1, fp);
		(void)pp_write_context(&(ocon->ibpkey_list[i].con), fp);
	}

	return 0;
}

static int __attribute__ ((unused)) pp_write_ocon_ibendport(policy_ocontext_t *ocon, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(ocon->ibendport_num), sizeof(ocon->ibendport_num), 1, fp);

	for (i = 0; i < ocon->ibendport_num; ++i) {
		(void)put_entry(&(ocon->ibendport_list[i].dev_name_len), sizeof(ocon->ibendport_list[i].dev_name_len),
				1, fp);
		(void)put_entry(&(ocon->ibendport_list[i].port), sizeof(ocon->ibendport_list[i].port), 1, fp);

		if (ocon->ibendport_list[i].dev_name_len) {
			(void)put_entry(ocon->ibendport_list[i].dev_name, 1, ocon->ibendport_list[i].dev_name_len, fp);
		}

		(void)pp_write_context(&(ocon->ibendport_list[i].con), fp);
	}

	return 0;
}

static int pp_write_genfs_item_helper(policy_genfs_ocon_item_t *item, struct policy_file *fp)
{
	(void)put_entry(&(item->name_len), sizeof(item->name_len), 1, fp);

	if (item->name_len) {
		(void)put_entry(item->name, 1, item->name_len, fp);
	}

	(void)put_entry(&(item->sclass), sizeof(item->sclass), 1, fp);

	(void)pp_write_context(&(item->con), fp);

	return 0;
}

static int pp_write_genfs_item(policy_genfs_item_t *item, struct policy_file *fp)
{
	uint32_t i;

	(void)put_entry(&(item->fstype_len), sizeof(item->fstype_len), 1, fp);

	if (item->fstype_len) {
		(void)put_entry(item->fstype, 1, item->fstype_len, fp);
	}

	(void)put_entry(&(item->ocon_num), sizeof(item->ocon_num), 1, fp);

	for (i = 0; i < item->ocon_num; ++i) {
		(void)pp_write_genfs_item_helper(&(item->ocon_list[i]), fp);
	}

	return 0;
}

static int pp_write_range_item(policy_range_item_t *item, struct policy_file *fp)
{
	(void)put_entry(&(item->source_type), sizeof(item->source_type), 1, fp);
	(void)put_entry(&(item->target_type), sizeof(item->target_type), 1, fp);
	(void)put_entry(&(item->target_class), sizeof(item->target_class), 1, fp);

	(void)pp_write_expanded_range(&(item->target_range), fp);

	return 0;
}

static int pp_write_typeattr_map_item(policy_typeattr_map_item_t *item, struct policy_file *fp)
{
	(void)pp_write_bitmap(&(item->attr_map), fp);

	return 0;
}

static int pp_free_bitmap(policy_bitmap_t *map)
{
	if (map && map->node_list) {
		pp_free(map->node_list);
		map->node_list = NULL;
	}

	return 0;
}

static int pp_free_perm(policy_perm_t *perm)
{
	if (perm && perm->name) {
		pp_free(perm->name);
		perm->name = NULL;
	}

	return 0;
}

static int pp_free_expr(policy_expression_t *expr)
{
	if (expr && (le32_to_cpu(expr->expr_type) == CEXPR_NAMES)) {
		(void)pp_free_bitmap(&(expr->names));
	}

	if (pp_version_num >= POLICYDB_VERSION_CONSTRAINT_NAMES) {
		(void)pp_free_bitmap(&(expr->type_names.types));
		(void)pp_free_bitmap(&(expr->type_names.negset));
	}

	return 0;
}

static int pp_free_cons(policy_constraints_t *cons)
{
	uint32_t i;

	if (cons && cons->expression_list) {
		for (i = 0; i < cons->expression_num; ++i) {
			(void)pp_free_expr(&(cons->expression_list[i]));
		}

		pp_free(cons->expression_list);
		cons->expression_list = NULL;
	}

	return 0;
}

static int pp_free_expanded_range(policy_exp_range_t *range)
{
	(void)pp_free_bitmap(&(range->cat_low));

	if (range && (le32_to_cpu(range->sens_num) > 1)) {
		(void)pp_free_bitmap(&(range->cat_high));
	}

	return 0;
}

static int pp_free_expanded_dfltlevel(policy_exp_dfltlevel_t *level)
{
	(void)pp_free_bitmap(&(level->cat));

	return 0;
}

static int pp_free_context(policy_context_t *con)
{
	(void)pp_free_expanded_range(&(con->exp_range));

	return 0;
}

static int pp_free_common_helper(policy_common_t *common)
{
	uint32_t i;

	if (!common) {
		return 0;
	}

	if (common->name) {
		pp_free(common->name);
		common->name = NULL;
	}

	for (i = 0; i < common->element_num; ++i) {
		(void)pp_free_perm(&(common->perm_list[i]));
	}

	if (common->perm_list) {
		pp_free(common->perm_list);
		common->perm_list = NULL;
	}

	return 0;
}

static int pp_free_common(policy_db_t *db)
{
	uint32_t i;
	policy_common_t *list = db->symtab_common.attr_list;

	for (i = 0; i < db->symtab_common.element_num; ++i) {
		(void)pp_free_common_helper(&(list[i]));
	}

	if (db->symtab_common.attr_list) {
		pp_free(db->symtab_common.attr_list);
		db->symtab_common.attr_list = NULL;
	}

	return 0;
}

static int pp_free_classes_helper(policy_classes_t *classes)
{
	uint32_t i;

	if (!classes) {
		return 0;
	}

	if (classes->name) {
		pp_free(classes->name);
		classes->name = NULL;
	}

	if (le32_to_cpu(classes->common_name_len) > 0) {
		if (classes->common_name) {
			pp_free(classes->common_name);
			classes->common_name = NULL;
		}
	}

	for (i = 0; i < classes->element_num; ++i) {
		(void)pp_free_perm(&(classes->perm_list[i]));
	}

	if (classes->perm_list) {
		pp_free(classes->perm_list);
		classes->perm_list = NULL;
	}

	for (i = 0; i < classes->constraints_num; ++i) {
		(void)pp_free_cons(&(classes->constraints_list[i]));
	}

	if (classes->constraints_list) {
		pp_free(classes->constraints_list);
		classes->constraints_list = NULL;
	}

	for (i = 0; i < classes->validatetrans_num; ++i) {
		(void)pp_free_cons(&(classes->validatetrans_list[i]));
	}

	if (classes->validatetrans_list) {
		pp_free(classes->validatetrans_list);
		classes->validatetrans_list = NULL;
	}

	return 0;
}

static int pp_free_classes(policy_db_t *db)
{
	uint32_t i;
	policy_classes_t *list = db->symtab_classes.attr_list;

	for (i = 0; i < db->symtab_classes.element_num; ++i) {
		(void)pp_free_classes_helper(&(list[i]));
	}

	if (db->symtab_classes.attr_list) {
		pp_free(db->symtab_classes.attr_list);
		db->symtab_classes.attr_list = NULL;
	}

	return 0;
}

static int pp_free_roles_helper(policy_roles_t *roles)
{
	if (!roles) {
		return 0;
	}

	if (roles->name) {
		pp_free(roles->name);
		roles->name = NULL;
	}

	(void)pp_free_bitmap(&(roles->dominates));
	(void)pp_free_bitmap(&(roles->types));

	return 0;
}

static int pp_free_roles(policy_db_t *db)
{
	uint32_t i;
	policy_roles_t *list = db->symtab_roles.attr_list;

	for (i = 0; i < db->symtab_roles.element_num; ++i) {
		(void)pp_free_roles_helper(&(list[i]));
	}

	if (db->symtab_roles.attr_list) {
		pp_free(db->symtab_roles.attr_list);
		db->symtab_roles.attr_list = NULL;
	}

	return 0;
}

static int pp_free_types_helper(policy_types_t *types)
{
	if (types && types->name) {
		pp_free(types->name);
		types->name = NULL;
	}

	return 0;
}

static int pp_free_types(policy_db_t *db)
{
	uint32_t i;
	policy_types_t *list = db->symtab_types.attr_list;

	for (i = 0; i < db->symtab_types.element_num; ++i) {
		(void)pp_free_types_helper(&(list[i]));
	}

	if (db->symtab_types.attr_list) {
		pp_free(db->symtab_types.attr_list);
		db->symtab_types.attr_list = NULL;
	}

	return 0;
}

static int pp_free_users_helper(policy_users_t *users)
{
	if (!users) {
		return 0;
	}

	if (users->name) {
		pp_free(users->name);
		users->name = NULL;
	}

	(void)pp_free_bitmap(&(users->roles));
	(void)pp_free_expanded_range(&(users->exp_range));
	(void)pp_free_expanded_dfltlevel(&(users->exp_dfltlevel));

	return 0;
}

static int pp_free_users(policy_db_t *db)
{
	uint32_t i;
	policy_users_t *list = db->symtab_users.attr_list;

	for (i = 0; i < db->symtab_users.element_num; ++i) {
		(void)pp_free_users_helper(&(list[i]));
	}

	if (db->symtab_users.attr_list) {
		pp_free(db->symtab_users.attr_list);
		db->symtab_users.attr_list = NULL;
	}

	return 0;
}

static int pp_free_bools_helper(policy_bools_t *bools)
{
	if (bools && bools->name) {
		pp_free(bools->name);
		bools->name = NULL;
	}

	return 0;
}

static int pp_free_bools(policy_db_t *db)
{
	uint32_t i;
	policy_bools_t *list = db->symtab_bools.attr_list;

	for (i = 0; i < db->symtab_bools.element_num; ++i) {
		(void)pp_free_bools_helper(&(list[i]));
	}

	if (db->symtab_bools.attr_list) {
		pp_free(db->symtab_bools.attr_list);
		db->symtab_bools.attr_list = NULL;
	}

	return 0;
}

static int pp_free_levels_helper(policy_levels_t *levels)
{
	if (!levels) {
		return 0;
	}

	if (levels->name) {
		pp_free(levels->name);
		levels->name = NULL;
	}

	(void)pp_free_expanded_dfltlevel(&(levels->level));

	return 0;
}

static int pp_free_levels(policy_db_t *db)
{
	uint32_t i;
	policy_levels_t *list = db->symtab_levels.attr_list;

	for (i = 0; i < db->symtab_levels.element_num; ++i) {
		(void)pp_free_levels_helper(&(list[i]));
	}

	if (db->symtab_levels.attr_list) {
		pp_free(db->symtab_levels.attr_list);
		db->symtab_levels.attr_list = NULL;
	}

	return 0;
}

static int pp_free_cats_helper(policy_cats_t *cats)
{
	if (cats && cats->name) {
		pp_free(cats->name);
		cats->name = NULL;
	}

	return 0;
}

static int pp_free_cats(policy_db_t *db)
{
	uint32_t i;
	policy_cats_t *list = db->symtab_cats.attr_list;

	for (i = 0; i < db->symtab_cats.element_num; ++i) {
		(void)pp_free_cats_helper(&(list[i]));
	}

	if (db->symtab_cats.attr_list) {
		pp_free(db->symtab_cats.attr_list);
		db->symtab_cats.attr_list = NULL;
	}

	return 0;
}

static int pp_free_avtab_item(policy_avtab_item_t *item)
{
	/* Do nothing here */
	return 0;
}

static int pp_free_cond_expr(policy_cond_expr_t *expr)
{
	/* Do nothing here */
	return 0;
}

static int pp_free_condlist_item(policy_condlist_item_t *item)
{
	uint32_t i;

	if (!item) {
		return 0;
	}

	for (i = 0; i < item->expression_num; ++i) {
		(void)pp_free_cond_expr(&(item->expression_list[i]));
	}

	if (item->expression_list) {
		pp_free(item->expression_list);
		item->expression_list = NULL;
	}

	for (i = 0; i < item->true_list.element_num; ++i) {
		(void)pp_free_avtab_item(&(item->true_list.av_list[i]));
	}

	if (item->true_list.av_list) {
		pp_free(item->true_list.av_list);
		item->true_list.av_list = NULL;
	}

	for (i = 0; i < item->false_list.element_num; ++i) {
		(void)pp_free_avtab_item(&(item->false_list.av_list[i]));
	}

	if (item->false_list.av_list) {
		pp_free(item->false_list.av_list);
		item->false_list.av_list = NULL;
	}

	return 0;
}

static int pp_free_roletrans_item(policy_roletrans_item_t *item)
{
	/* Do nothing here */
	return 0;
}

static int pp_free_roleallow_item(policy_roleallow_item_t *item)
{
	/* Do nothing here */
	return 0;
}

static int pp_free_filenametrans_item(policy_filenametrans_item_t *item)
{
	if (item && item->name) {
		pp_free(item->name);
		item->name = NULL;
	}

	return 0;
}

static int pp_free_ocon_isid(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->isid_num; ++i) {
		(void)pp_free_context(&(ocon->isid_list[i].con));
	}

	if (ocon->isid_list) {
		pp_free(ocon->isid_list);
		ocon->isid_list = NULL;
	}

	return 0;
}

static int pp_free_ocon_fs(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->fs_num; ++i) {
		if (ocon->fs_list[i].name) {
			pp_free(ocon->fs_list[i].name);
			ocon->fs_list[i].name = NULL;
		}

		(void)pp_free_context(&(ocon->fs_list[i].con_0));
		(void)pp_free_context(&(ocon->fs_list[i].con_1));
	}

	if (ocon->fs_list) {
		pp_free(ocon->fs_list);
		ocon->fs_list = NULL;
	}

	return 0;
}

static int pp_free_ocon_port(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->port_num; ++i) {
		(void)pp_free_context(&(ocon->port_list[i].con));
	}

	if (ocon->port_list) {
		pp_free(ocon->port_list);
		ocon->port_list = NULL;
	}

	return 0;
}

static int pp_free_ocon_netif(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->netif_num; ++i) {
		if (ocon->netif_list[i].name) {
			pp_free(ocon->netif_list[i].name);
			ocon->netif_list[i].name = NULL;
		}

		(void)pp_free_context(&(ocon->netif_list[i].con_0));
		(void)pp_free_context(&(ocon->netif_list[i].con_1));
	}

	if (ocon->netif_list) {
		pp_free(ocon->netif_list);
		ocon->netif_list = NULL;
	}

	return 0;
}

static int pp_free_ocon_node(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->node_num; ++i) {
		(void)pp_free_context(&(ocon->node_list[i].con));
	}

	if (ocon->node_list) {
		pp_free(ocon->node_list);
		ocon->node_list = NULL;
	}

	return 0;
}

static int pp_free_ocon_fsuse(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->fsuse_num; ++i) {
		if (ocon->fsuse_list[i].name) {
			pp_free(ocon->fsuse_list[i].name);
			ocon->fsuse_list[i].name = NULL;
		}

		(void)pp_free_context(&(ocon->fsuse_list[i].con));
	}

	if (ocon->fsuse_list) {
		pp_free(ocon->fsuse_list);
		ocon->fsuse_list = NULL;
	}

	return 0;
}

static int pp_free_ocon_node6(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->node6_num; ++i) {
		(void)pp_free_context(&(ocon->node6_list[i].con));
	}

	if (ocon->node6_list) {
		pp_free(ocon->node6_list);
		ocon->node6_list = NULL;
	}

	return 0;
}

static int __attribute__ ((unused)) pp_free_ocon_ibpkey(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->ibpkey_num; ++i) {
		(void)pp_free_context(&(ocon->ibpkey_list[i].con));
	}

	if (ocon->ibpkey_list) {
		pp_free(ocon->ibpkey_list);
		ocon->ibpkey_list = NULL;
	}

	return 0;
}

static int __attribute__ ((unused)) pp_free_ocon_ibendport(policy_ocontext_t *ocon)
{
	uint32_t i;

	if (!ocon) {
		return 0;
	}

	for (i = 0; i < ocon->ibendport_num; ++i) {
		if (ocon->ibendport_list[i].dev_name) {
			pp_free(ocon->ibendport_list[i].dev_name);
			ocon->ibendport_list[i].dev_name = NULL;
		}

		(void)pp_free_context(&(ocon->ibendport_list[i].con));
	}

	if (ocon->ibendport_list) {
		pp_free(ocon->ibendport_list);
		ocon->ibendport_list = NULL;
	}

	return 0;
}

static int pp_free_genfs_item_helper(policy_genfs_ocon_item_t *item)
{
	if (!item) {
		return 0;
	}

	if (item->name) {
		pp_free(item->name);
		item->name = NULL;
	}

	(void)pp_free_context(&(item->con));

	return 0;
}

static int pp_free_genfs_item(policy_genfs_item_t *item)
{
	uint32_t i;

	if (!item) {
		return 0;
	}

	if (item->fstype) {
		pp_free(item->fstype);
		item->fstype = NULL;
	}

	for (i = 0; i < item->ocon_num; ++i) {
		(void)pp_free_genfs_item_helper(&(item->ocon_list[i]));
	}

	if (item->ocon_list) {
		pp_free(item->ocon_list);
		item->ocon_list = NULL;
	}

	return 0;
}

static int pp_free_range_item(policy_range_item_t *item)
{
	(void)pp_free_expanded_range(&(item->target_range));

	return 0;
}

static int pp_free_typeattr_map_item(policy_typeattr_map_item_t *item)
{
	(void)pp_free_bitmap(&(item->attr_map));

	return 0;
}

static int pp_parse_header(struct policy_file *fp, policy_db_t *db)
{
	int rc;

	rc = next_entry(&(db->magic), fp, sizeof(db->magic));
	if (rc) {
		return rc;
	}

	if (le32_to_cpu(db->magic) != POLICYDB_MAGIC) {
		pr_err("SELinux: policydb magic number 0x%x does "
		       "not match expected magic number 0x%x\n",
		       le32_to_cpu(db->magic), POLICYDB_MAGIC);
		return -EINVAL;
	}

	rc = next_entry(&(db->target_str_len), fp, sizeof(db->target_str_len));
	if (rc) {
		goto pp_parse_header_fail;
	}

	if (le32_to_cpu(db->target_str_len) != strlen(POLICYDB_STRING)) {
		pr_err("SELinux: policydb string length %d does not "
		       "match expected length %zu\n",
		       le32_to_cpu(db->target_str_len), strlen(POLICYDB_STRING));
		rc = -EINVAL;
		goto pp_parse_header_fail;
	}

	if (db->target_str_len) {
		db->target_str = pp_zalloc(db->target_str_len + 1);
		if (!db->target_str) {
			pr_err("SELinux: unable to allocate memory for policydb "
			       "string of length %d\n", db->target_str_len + 1);
			rc = -ENOMEM;
			goto pp_parse_header_fail;
		}

		rc = next_entry(db->target_str, fp, db->target_str_len);
		if (rc) {
			goto pp_parse_header_fail;
		}

		if (strcmp(db->target_str, POLICYDB_STRING)) {
			pr_err("SELinux: policydb string %s does not match "
			       "string %s\n", db->target_str, POLICYDB_STRING);
			rc = -EINVAL;
			goto pp_parse_header_fail;
		}
	}

	rc = next_entry(&(db->version_num), fp, sizeof(db->version_num));
	if (rc) {
		goto pp_parse_header_fail;
	}
	pp_version_num = le32_to_cpu(db->version_num);

	if (le32_to_cpu(db->version_num) < POLICYDB_VERSION_MIN
			|| le32_to_cpu(db->version_num) > POLICYDB_VERSION_MAX_SUPPORTED) {
		pr_err("SELinux: policydb version %d does not match "
		       "version range %d-%d\n",
		       le32_to_cpu(db->version_num), POLICYDB_VERSION_MIN, POLICYDB_VERSION_MAX_SUPPORTED);
		rc = WARNING_HEADER_UNSUPPORTED;
		goto pp_parse_header_fail;
	}

	rc = next_entry(&(db->config), fp, sizeof(db->config));
	if (rc) {
		goto pp_parse_header_fail;
	}

	if ((le32_to_cpu(db->config) & POLICYDB_CONFIG_MLS)) {
		if (le32_to_cpu(db->version_num) < POLICYDB_VERSION_MLS) {
			pr_err("SELinux: security policydb version %d "
						 "(MLS) not backwards compatible\n",
						 le32_to_cpu(db->version_num));
			rc = -EINVAL;
			goto pp_parse_header_fail;
		}
	}

	rc = next_entry(&(db->sym_num), fp, sizeof(db->sym_num));
	if (rc) {
		goto pp_parse_header_fail;
	}

	rc = next_entry(&(db->ocon_num), fp, sizeof(db->ocon_num));
	if (rc) {
		goto pp_parse_header_fail;
	}

	return 0;

pp_parse_header_fail:

	if (db->target_str) {
		pp_free(db->target_str);
		db->target_str = NULL;
	}

	return rc;
}

static int pp_parse_policycaps(struct policy_file *fp, policy_db_t *db)
{
	return pp_read_bitmap(fp, &(db->policycaps));
}

static int pp_parse_permissive_map(struct policy_file *fp, policy_db_t *db)
{
	return pp_read_bitmap(fp, &(db->permissive_map));
}

static int pp_parse_symtab(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	int rc;

	for (i = 0; i < db->sym_num; ++i) {
		rc = pp_symtab_read_map[i](fp, db);
		if (rc) {
			return rc;
		}
	}

	return 0;
}

static int pp_parse_avtab(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_avtab_item_t *list = NULL;
	int rc;

	rc = next_entry(&(db->avtab.element_num), fp, sizeof(db->avtab.element_num));
	if (rc) {
		return rc;
	}

	if (!db->avtab.element_num) {
		return 0;
	}

	db->avtab.attr_list = pp_zalloc(db->avtab.element_num * sizeof(policy_avtab_item_t));
	if (!db->avtab.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->avtab.element_num * sizeof(policy_avtab_item_t)));
		return -ENOMEM;
	}
	list = (policy_avtab_item_t *)db->avtab.attr_list;

	for (i = 0; i < db->avtab.element_num; ++i) {
		rc = pp_read_avtab_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_avtab_fail;
		}
	}

	return 0;

pp_parse_avtab_fail:

	if (db->avtab.attr_list) {
		pp_free(db->avtab.attr_list);
		db->avtab.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_condlist(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_condlist_item_t *list = NULL;
	int rc;

	rc = next_entry(&(db->condlist.element_num), fp, sizeof(db->condlist.element_num));
	if (rc) {
		return rc;
	}

	if (!db->condlist.element_num) {
		return 0;
	}

	db->condlist.attr_list = pp_zalloc(db->condlist.element_num * sizeof(policy_condlist_item_t));
	if (!db->condlist.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->condlist.element_num * sizeof(policy_condlist_item_t)));
		return -ENOMEM;
	}
	list = (policy_condlist_item_t *)db->condlist.attr_list;

	for (i = 0; i < db->condlist.element_num; ++i) {
		rc = pp_read_condlist_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_condlist_fail;
		}
	}

	return 0;

pp_parse_condlist_fail:

	if (db->condlist.attr_list) {
		pp_free(db->condlist.attr_list);
		db->condlist.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_roletrans(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_roletrans_item_t *list = NULL;
	int rc;

	rc = next_entry(&(db->roletrans.element_num), fp, sizeof(db->roletrans.element_num));
	if (rc) {
		return rc;
	}

	if (!db->roletrans.element_num) {
		return 0;
	}

	db->roletrans.attr_list = pp_zalloc(db->roletrans.element_num * sizeof(policy_roletrans_item_t));
	if (!db->roletrans.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->roletrans.element_num * sizeof(policy_roletrans_item_t)));
		return -ENOMEM;
	}
	list = (policy_roletrans_item_t *)db->roletrans.attr_list;

	for (i = 0; i < db->roletrans.element_num; ++i) {
		rc = pp_read_roletrans_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_roletrans_fail;
		}
	}

	return 0;

pp_parse_roletrans_fail:

	if (db->roletrans.attr_list) {
		pp_free(db->roletrans.attr_list);
		db->roletrans.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_roleallow(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_roleallow_item_t *list = NULL;
	int rc;

	rc = next_entry(&(db->roleallow.element_num), fp, sizeof(db->roleallow.element_num));
	if (rc) {
		return rc;
	}

	if (!db->roleallow.element_num) {
		return 0;
	}

	db->roleallow.attr_list = pp_zalloc(db->roleallow.element_num * sizeof(policy_roleallow_item_t));
	if (!db->roleallow.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->roleallow.element_num * sizeof(policy_roleallow_item_t)));
		return -ENOMEM;
	}
	list = (policy_roleallow_item_t *)db->roleallow.attr_list;

	for (i = 0; i < db->roleallow.element_num; ++i) {
		rc = pp_read_roleallow_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_roleallow_fail;
		}
	}

	return 0;

pp_parse_roleallow_fail:

	if (db->roleallow.attr_list) {
		pp_free(db->roleallow.attr_list);
		db->roleallow.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_filenametrans(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_filenametrans_item_t *list = NULL;
	int rc;

	rc = next_entry(&(db->filenametrans.element_num), fp, sizeof(db->filenametrans.element_num));
	if (rc) {
		return rc;
	}

	if (!db->filenametrans.element_num) {
		return 0;
	}

	db->filenametrans.attr_list = pp_zalloc(db->filenametrans.element_num * sizeof(policy_filenametrans_item_t));
	if (!db->filenametrans.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->filenametrans.element_num * sizeof(policy_filenametrans_item_t)));
		return -ENOMEM;
	}
	list = (policy_filenametrans_item_t *)db->filenametrans.attr_list;

	for (i = 0; i < db->filenametrans.element_num; ++i) {
		rc = pp_read_filenametrans_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_filenametrans_fail;
		}
	}

	return 0;

pp_parse_filenametrans_fail:

	if (db->filenametrans.attr_list) {
		pp_free(db->filenametrans.attr_list);
		db->filenametrans.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_ocontext(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	int rc;

	db->ocontext.element_num = db->ocon_num;

	for (i = 0; i < db->ocontext.element_num; ++i) {
		switch (i) {
		case OCON_ISID:
			rc = pp_read_ocon_isid(fp, &(db->ocontext));
			break;
		case OCON_FS:
			rc = pp_read_ocon_fs(fp, &(db->ocontext));
			break;
		case OCON_PORT:
			rc = pp_read_ocon_port(fp, &(db->ocontext));
			break;
		case OCON_NETIF:
			rc = pp_read_ocon_netif(fp, &(db->ocontext));
			break;
		case OCON_NODE:
			rc = pp_read_ocon_node(fp, &(db->ocontext));
			break;
		case OCON_FSUSE:
			rc = pp_read_ocon_fsuse(fp, &(db->ocontext));
			break;
		case OCON_NODE6:
			rc = pp_read_ocon_node6(fp, &(db->ocontext));
			break;
#if 0 /* DISUSED */
		case OCON_IBPKEY:
			rc = pp_read_ocon_ibpkey(fp, &(db->ocontext));
			break;
		case OCON_IBENDPORT:
			rc = pp_read_ocon_ibendport(fp, &(db->ocontext));
			break;
#endif
		default:
			break;
		}

		if (rc) {
			return rc;
		}
	}

	return 0;
}

static int pp_parse_genfs(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_genfs_item_t *list = NULL;
	int rc;

	rc = next_entry(&(db->genfs.element_num), fp, sizeof(db->genfs.element_num));
	if (rc) {
		return rc;
	}

	if (!db->genfs.element_num) {
		return 0;
	}

	db->genfs.attr_list = pp_zalloc(db->genfs.element_num * sizeof(policy_genfs_item_t));
	if (!db->genfs.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->genfs.element_num * sizeof(policy_genfs_item_t)));
		return -ENOMEM;
	}
	list = (policy_genfs_item_t *)db->genfs.attr_list;

	for (i = 0; i < db->genfs.element_num; ++i) {
		rc = pp_read_genfs_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_genfs_fail;
		}
	}

	return 0;

pp_parse_genfs_fail:

	if (db->genfs.attr_list) {
		pp_free(db->genfs.attr_list);
		db->genfs.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_range(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_range_item_t *list = NULL;
	int rc;

	rc = next_entry(&(db->range.element_num), fp, sizeof(db->range.element_num));
	if (rc) {
		return rc;
	}

	if (!db->range.element_num) {
		return 0;
	}

	db->range.attr_list = pp_zalloc(db->range.element_num * sizeof(policy_range_item_t));
	if (!db->range.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->range.element_num * sizeof(policy_range_item_t)));
		return -ENOMEM;
	}
	list = (policy_range_item_t *)db->range.attr_list;

	for (i = 0; i < db->range.element_num; ++i) {
		rc = pp_read_range_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_range_fail;
		}
	}

	return 0;

pp_parse_range_fail:

	if (db->range.attr_list) {
		pp_free(db->range.attr_list);
		db->range.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_typeattr_map(struct policy_file *fp, policy_db_t *db)
{
	uint32_t i;
	policy_typeattr_map_item_t *list = NULL;
	int rc;

	db->typeattr_map.element_num = db->symtab_types.primary_name_num;
	if (!db->typeattr_map.element_num) {
		return 0;
	}

	db->typeattr_map.attr_list = pp_zalloc(db->typeattr_map.element_num * sizeof(policy_typeattr_map_item_t));
	if (!db->typeattr_map.attr_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(db->typeattr_map.element_num * sizeof(policy_typeattr_map_item_t)));
		return -ENOMEM;
	}
	list = (policy_typeattr_map_item_t *)db->typeattr_map.attr_list;

	for (i = 0; i < db->typeattr_map.element_num; ++i) {
		rc = pp_read_typeattr_map_item(fp, &list[i]);
		if (rc) {
			goto pp_parse_typeattr_map_fail;
		}
	}

	return 0;

pp_parse_typeattr_map_fail:

	if (db->typeattr_map.attr_list) {
		pp_free(db->typeattr_map.attr_list);
		db->typeattr_map.attr_list = NULL;
	}

	return rc;
}

static int pp_parse_policy(struct policy_file *fp, policy_db_t *db)
{
	int rc;

	rc = pp_parse_header(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb header\n");
		return rc;
	}

	rc = pp_parse_policycaps(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb policy caps\n");
		return rc;
	}

	rc = pp_parse_permissive_map(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb permissive map\n");
		return rc;
	}

	rc = pp_parse_symtab(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb symbol table\n");
		return rc;
	}

	rc = pp_parse_avtab(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb av table\n");
		return rc;
	}

	rc = pp_parse_condlist(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb conditional list\n");
		return rc;
	}

	rc = pp_parse_roletrans(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb role transition\n");
		return rc;
	}

	rc = pp_parse_roleallow(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb role allow\n");
		return rc;
	}

	rc = pp_parse_filenametrans(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb file name transition\n");
		return rc;
	}

	rc = pp_parse_ocontext(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb ocontext\n");
		return rc;
	}

	rc = pp_parse_genfs(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb genfs\n");
		return rc;
	}

	rc = pp_parse_range(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb range\n");
		return rc;
	}

	rc = pp_parse_typeattr_map(fp, db);
	if (rc) {
		pr_err("SELinux: failed to parse policydb type attribute map\n");
		return rc;
	}

	return 0;
}

static int pp_parse_appendix(struct policy_file *fp, policy_appendix_t *appx)
{
	uint32_t i;
	policy_appendix_types_t *types_list = NULL;
	policy_appendix_classes_t *classes_list = NULL;
	int rc;

	rc = next_entry(&(appx->types_num), fp, sizeof(appx->types_num));
	if (rc) {
		return rc;
	}

	if (!appx->types_num) {
		return -EINVAL;
	}

	appx->types_list = pp_zalloc(appx->types_num * sizeof(policy_appendix_types_t));
	if (!appx->types_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(appx->types_num * sizeof(policy_appendix_types_t)));
		return -ENOMEM;
	}
	types_list = (policy_appendix_types_t *)appx->types_list;

	for (i = 0; i < appx->types_num; ++i) {
		rc = pp_read_appendix_type(fp, &types_list[i]);
		if (rc) {
			goto pp_parse_appendix_fail;
		}
	}

	rc = next_entry(&(appx->classes_num), fp, sizeof(appx->classes_num));
	if (rc) {
		goto pp_parse_appendix_fail;
	}

	if (!appx->classes_num) {
		rc = -EINVAL;
		goto pp_parse_appendix_fail;
	}

	appx->classes_list = pp_zalloc(appx->classes_num * sizeof(policy_appendix_classes_t));
	if (!appx->classes_list) {
		pr_err("SELinux: unable to allocate memory for policydb "
				"attribute list of length %u\n",
				(uint32_t)(appx->classes_num * sizeof(policy_appendix_classes_t)));
		rc = -ENOMEM;
		goto pp_parse_appendix_fail;
	}
	classes_list = (policy_appendix_classes_t *)appx->classes_list;

	for (i = 0; i < appx->classes_num; ++i) {
		rc = pp_read_appendix_class(fp, &classes_list[i]);
		if (rc) {
			goto pp_parse_appendix_fail;
		}
	}

	return 0;

pp_parse_appendix_fail:

	if (appx->classes_list) {
		pp_free(appx->classes_list);
		appx->classes_list = NULL;
	}

	if (appx->types_list) {
		pp_free(appx->types_list);
		appx->types_list = NULL;
	}

	return rc;
}

static int pp_build_header(policy_db_t *db, struct policy_file *fp)
{
	(void)put_entry(&(db->magic), sizeof(db->magic), 1, fp);
	(void)put_entry(&(db->target_str_len), sizeof(db->target_str_len), 1, fp);

	if (db->target_str_len) {
		(void)put_entry(db->target_str, 1, db->target_str_len, fp);
	}

	(void)put_entry(&(db->version_num), sizeof(db->version_num), 1, fp);
	(void)put_entry(&(db->config), sizeof(db->config), 1, fp);
	(void)put_entry(&(db->sym_num), sizeof(db->sym_num), 1, fp);
	(void)put_entry(&(db->ocon_num), sizeof(db->ocon_num), 1, fp);

	return 0;
}

static int pp_build_policycaps(policy_db_t *db, struct policy_file *fp)
{
	return pp_write_bitmap(&(db->policycaps), fp);
}

static int pp_build_permissive_map(policy_db_t *db, struct policy_file *fp)
{
	return pp_write_bitmap(&(db->permissive_map), fp);
}

static int pp_build_symtab(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;

	for (i = 0; i < db->sym_num; ++i) {
		(void)pp_symtab_write_map[i](db, fp);
	}

	return 0;
}

static int pp_build_avtab(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;

	(void)put_entry(&(db->avtab.element_num), sizeof(db->avtab.element_num), 1, fp);

	for (i = 0; i < db->avtab.element_num; ++i) {
		(void)pp_write_avtab_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_condlist(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_condlist_item_t *list = (policy_condlist_item_t *)db->condlist.attr_list;

	(void)put_entry(&(db->condlist.element_num), sizeof(db->condlist.element_num), 1, fp);

	for (i = 0; i < db->condlist.element_num; ++i) {
		(void)pp_write_condlist_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_roletrans(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_roletrans_item_t *list = (policy_roletrans_item_t *)db->roletrans.attr_list;

	(void)put_entry(&(db->roletrans.element_num), sizeof(db->roletrans.element_num), 1, fp);

	for (i = 0; i < db->roletrans.element_num; ++i) {
		(void)pp_write_roletrans_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_roleallow(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_roleallow_item_t *list = (policy_roleallow_item_t *)db->roleallow.attr_list;

	(void)put_entry(&(db->roleallow.element_num), sizeof(db->roleallow.element_num), 1, fp);

	for (i = 0; i < db->roleallow.element_num; ++i) {
		(void)pp_write_roleallow_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_filenametrans(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_filenametrans_item_t *list = (policy_filenametrans_item_t *)db->filenametrans.attr_list;

	(void)put_entry(&(db->filenametrans.element_num), sizeof(db->filenametrans.element_num), 1, fp);

	for (i = 0; i < db->filenametrans.element_num; ++i) {
		(void)pp_write_filenametrans_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_ocontext(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;

	for (i = 0; i < db->ocontext.element_num; ++i) {
		switch (i) {
		case OCON_ISID:
			(void)pp_write_ocon_isid(&(db->ocontext), fp);
			break;
		case OCON_FS:
			(void)pp_write_ocon_fs(&(db->ocontext), fp);
			break;
		case OCON_PORT:
			(void)pp_write_ocon_port(&(db->ocontext), fp);
			break;
		case OCON_NETIF:
			(void)pp_write_ocon_netif(&(db->ocontext), fp);
			break;
		case OCON_NODE:
			(void)pp_write_ocon_node(&(db->ocontext), fp);
			break;
		case OCON_FSUSE:
			(void)pp_write_ocon_fsuse(&(db->ocontext), fp);
			break;
		case OCON_NODE6:
			(void)pp_write_ocon_node6(&(db->ocontext), fp);
			break;
#if 0 /* DISUSED */
		case OCON_IBPKEY:
			(void)pp_write_ocon_ibpkey(&(db->ocontext), fp);
			break;
		case OCON_IBENDPORT:
			(void)pp_write_ocon_ibendport(&(db->ocontext), fp);
			break;
#endif
		default:
			break;
		}
	}

	return 0;
}

static int pp_build_genfs(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_genfs_item_t *list = (policy_genfs_item_t *)db->genfs.attr_list;

	(void)put_entry(&(db->genfs.element_num), sizeof(db->genfs.element_num), 1, fp);

	for (i = 0; i < db->genfs.element_num; ++i) {
		(void)pp_write_genfs_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_range(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_range_item_t *list = (policy_range_item_t *)db->range.attr_list;

	(void)put_entry(&(db->range.element_num), sizeof(db->range.element_num), 1, fp);

	for (i = 0; i < db->range.element_num; ++i) {
		(void)pp_write_range_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_typeattr_map(policy_db_t *db, struct policy_file *fp)
{
	uint32_t i;
	policy_typeattr_map_item_t *list = (policy_typeattr_map_item_t *)db->typeattr_map.attr_list;

	for (i = 0; i < db->typeattr_map.element_num; ++i) {
		(void)pp_write_typeattr_map_item(&list[i], fp);
	}

	return 0;
}

static int pp_build_policy(policy_db_t *db, struct policy_file *fp)
{
	int rc;

	rc = pp_build_header(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb header\n");
		return rc;
	}

	rc = pp_build_policycaps(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb policy caps\n");
		return rc;
	}

	rc = pp_build_permissive_map(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb permissive map\n");
		return rc;
	}

	rc = pp_build_symtab(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb symbol table\n");
		return rc;
	}

	rc = pp_build_avtab(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb av table\n");
		return rc;
	}

	rc = pp_build_condlist(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb conditional list\n");
		return rc;
	}

	rc = pp_build_roletrans(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb role transition\n");
		return rc;
	}

	rc = pp_build_roleallow(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb role allow\n");
		return rc;
	}

	rc = pp_build_filenametrans(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb file name transition\n");
		return rc;
	}

	rc = pp_build_ocontext(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb ocontext\n");
		return rc;
	}

	rc = pp_build_genfs(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb genfs\n");
		return rc;
	}

	rc = pp_build_range(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb range\n");
		return rc;
	}

	rc = pp_build_typeattr_map(db, fp);
	if (rc) {
		pr_err("SELinux: failed to build policydb type attribute map\n");
		return rc;
	}

	return 0;
}

static int pp_free_header(policy_db_t *db)
{
	if (db && db->target_str) {
		pp_free(db->target_str);
		db->target_str = NULL;
	}

	return 0;
}

static int pp_free_policycaps(policy_db_t *db)
{
	(void)pp_free_bitmap(&(db->policycaps));

	return 0;
}

static int pp_free_permissive_map(policy_db_t *db)
{
	(void)pp_free_bitmap(&(db->permissive_map));

	return 0;
}

static int pp_free_symtab(policy_db_t *db)
{
	uint32_t i;

	if (!db) {
		return 0;
	}

	for (i = 0; i < db->sym_num; ++i) {
		(void)pp_symtab_free_map[i](db);
	}

	return 0;
}

static int pp_free_avtab(policy_db_t *db)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;

	for (i = 0; i < db->avtab.element_num; ++i) {
		(void)pp_free_avtab_item(&list[i]);
	}

	if (db->avtab.attr_list) {
		pp_free(db->avtab.attr_list);
		db->avtab.attr_list = NULL;
	}

	return 0;
}

static int pp_free_condlist(policy_db_t *db)
{
	uint32_t i;
	policy_condlist_item_t *list = (policy_condlist_item_t *)db->condlist.attr_list;

	for (i = 0; i < db->condlist.element_num; ++i) {
		(void)pp_free_condlist_item(&list[i]);
	}

	if (db->condlist.attr_list) {
		pp_free(db->condlist.attr_list);
		db->condlist.attr_list = NULL;
	}

	return 0;
}

static int pp_free_roletrans(policy_db_t *db)
{
	uint32_t i;
	policy_roletrans_item_t *list = (policy_roletrans_item_t *)db->roletrans.attr_list;

	for (i = 0; i < db->roletrans.element_num; ++i) {
		(void)pp_free_roletrans_item(&list[i]);
	}

	if (db->roletrans.attr_list) {
		pp_free(db->roletrans.attr_list);
		db->roletrans.attr_list = NULL;
	}

	return 0;
}

static int pp_free_roleallow(policy_db_t *db)
{
	uint32_t i;
	policy_roleallow_item_t *list = (policy_roleallow_item_t *)db->roleallow.attr_list;

	for (i = 0; i < db->roleallow.element_num; ++i) {
		(void)pp_free_roleallow_item(&list[i]);
	}

	if (db->roleallow.attr_list) {
		pp_free(db->roleallow.attr_list);
		db->roleallow.attr_list = NULL;
	}

	return 0;
}

static int pp_free_filenametrans(policy_db_t *db)
{
	uint32_t i;
	policy_filenametrans_item_t *list = (policy_filenametrans_item_t *)db->filenametrans.attr_list;

	for (i = 0; i < db->filenametrans.element_num; ++i) {
		(void)pp_free_filenametrans_item(&list[i]);
	}

	if (db->filenametrans.attr_list) {
		pp_free(db->filenametrans.attr_list);
		db->filenametrans.attr_list = NULL;
	}

	return 0;
}

static int pp_free_ocontext(policy_db_t *db)
{
	uint32_t i;

	if (!db) {
		return 0;
	}

	for (i = 0; i < db->ocontext.element_num; ++i) {
		switch (i) {
		case OCON_ISID:
			(void)pp_free_ocon_isid(&(db->ocontext));
			break;
		case OCON_FS:
			(void)pp_free_ocon_fs(&(db->ocontext));
			break;
		case OCON_PORT:
			(void)pp_free_ocon_port(&(db->ocontext));
			break;
		case OCON_NETIF:
			(void)pp_free_ocon_netif(&(db->ocontext));
			break;
		case OCON_NODE:
			(void)pp_free_ocon_node(&(db->ocontext));
			break;
		case OCON_FSUSE:
			(void)pp_free_ocon_fsuse(&(db->ocontext));
			break;
		case OCON_NODE6:
			(void)pp_free_ocon_node6(&(db->ocontext));
			break;
#if 0 /* DISUSED */
		case OCON_IBPKEY:
			(void)pp_free_ocon_ibpkey(&(db->ocontext));
			break;
		case OCON_IBENDPORT:
			(void)pp_free_ocon_ibendport(&(db->ocontext));
			break;
#endif
		default:
			break;
		}
	}

	return 0;
}

static int pp_free_genfs(policy_db_t *db)
{
	uint32_t i;
	policy_genfs_item_t *list = (policy_genfs_item_t *)db->genfs.attr_list;

	for (i = 0; i < db->genfs.element_num; ++i) {
		(void)pp_free_genfs_item(&list[i]);
	}

	if (db->genfs.attr_list) {
		pp_free(db->genfs.attr_list);
		db->genfs.attr_list = NULL;
	}

	return 0;
}

static int pp_free_range(policy_db_t *db)
{
	uint32_t i;
	policy_range_item_t *list = (policy_range_item_t *)db->range.attr_list;

	for (i = 0; i < db->range.element_num; ++i) {
		(void)pp_free_range_item(&list[i]);
	}

	if (db->range.attr_list) {
		pp_free(db->range.attr_list);
		db->range.attr_list = NULL;
	}

	return 0;
}

static int pp_free_typeattr_map(policy_db_t *db)
{
	uint32_t i;
	policy_typeattr_map_item_t *list = (policy_typeattr_map_item_t *)db->typeattr_map.attr_list;

	for (i = 0; i < db->typeattr_map.element_num; ++i) {
		(void)pp_free_typeattr_map_item(&list[i]);
	}

	if (db->typeattr_map.attr_list) {
		pp_free(db->typeattr_map.attr_list);
		db->typeattr_map.attr_list = NULL;
	}

	return 0;
}

static int pp_free_policy(policy_db_t *db)
{
	(void)pp_free_header(db);
	(void)pp_free_policycaps(db);
	(void)pp_free_permissive_map(db);
	(void)pp_free_symtab(db);
	(void)pp_free_avtab(db);
	(void)pp_free_condlist(db);
	(void)pp_free_roletrans(db);
	(void)pp_free_roleallow(db);
	(void)pp_free_filenametrans(db);
	(void)pp_free_ocontext(db);
	(void)pp_free_genfs(db);
	(void)pp_free_range(db);
	(void)pp_free_typeattr_map(db);

	return 0;
}

static int pp_free_appendix(policy_appendix_t *appx)
{
	uint32_t i, j;
	policy_appendix_types_t *types_list = (policy_appendix_types_t *)appx->types_list;
	policy_appendix_classes_t *classes_list = (policy_appendix_classes_t *)appx->classes_list;

	for (i = 0; i < appx->types_num; ++i) {
		if (types_list[i].name) {
			pp_free(types_list[i].name);
			types_list[i].name = NULL;
		}
	}

	if (appx->types_list) {
		pp_free(appx->types_list);
		appx->types_list = NULL;
	}

	for (i = 0; i < appx->classes_num; ++i) {
		if (classes_list[i].cname) {
			pp_free(classes_list[i].cname);
			classes_list[i].cname = NULL;
		}

		for (j = 0; j < classes_list[i].perms_num; ++j) {
			if (classes_list[i].perms[j].name) {
				pp_free(classes_list[i].perms[j].name);
				classes_list[i].perms[j].name = NULL;
			}
		}

		if (classes_list[i].perms) {
			pp_free(classes_list[i].perms);
			classes_list[i].perms = NULL;
		}
	}

	if (appx->classes_list) {
		pp_free(appx->classes_list);
		appx->classes_list = NULL;
	}

	return 0;
}

static const char *pp_class_to_name(policy_db_t *db, uint32_t class)
{
	uint32_t i;
	policy_classes_t *list = (policy_classes_t *)db->symtab_classes.attr_list;
	const char *name = NULL;

	for (i = 0; i < db->symtab_classes.element_num; ++i) {
		if (list[i].datums == class) {
			name = list[i].name;
			break;
		}
	}

	return name;
}

static const char *pp_type_to_name(policy_db_t *db, uint32_t type)
{
	uint32_t i;
	policy_types_t *list = (policy_types_t *)db->symtab_types.attr_list;
	const char *name = NULL;

	for (i = 0; i < db->symtab_types.element_num; ++i) {
		if ((list[i].datums == type) && (list[i].properties == TYPEDATUM_PROPERTY_PRIMARY)) {
			name = list[i].name;
			break;
		}
	}

	return name;
}

static uint32_t __attribute__ ((unused)) pp_name_to_class(policy_db_t *db, const char *name)
{
	uint32_t i;
	policy_classes_t *list = (policy_classes_t *)db->symtab_classes.attr_list;
	uint32_t class = 0;

	for (i = 0; i < db->symtab_classes.element_num; ++i) {
		if (!strcmp(list[i].name, name)) {
			class = list[i].datums;
			break;
		}
	}

	return class;
}

static uint32_t pp_name_to_type(policy_db_t *db, const char *name)
{
	uint32_t i;
	policy_types_t *list = (policy_types_t *)db->symtab_types.attr_list;
	uint32_t type = 0;

	for (i = 0; i < db->symtab_types.element_num; ++i) {
		if (!strcmp(list[i].name, name)) {
			type = list[i].datums;
			break;
		}
	}

	return type;
}

static uint32_t pp_name_to_perm(policy_db_t *db, pp_avc_desc_t *desc, const char *name)
{
	uint32_t i;
	size_t index;
	policy_common_t *common_list = (policy_common_t *)db->symtab_common.attr_list;
	policy_classes_t *classes_list = (policy_classes_t *)db->symtab_classes.attr_list;
	uint32_t perm = 0;
	bool found = false;

	if (!pp_find_class(db, desc, &index)) {
		return 0;
	}

	for (i = 0; i < classes_list[index].element_num; ++i) {
		if (!strcmp(classes_list[index].perm_list[i].name, name)) {
			perm = 1U << (classes_list[index].perm_list[i].datums - 1);
			found = true;
			break;
		}
	}

	if (!found && pp_find_common(db, desc, &index)) {
		for (i = 0; i < common_list[index].element_num; ++i) {
			if (!strcmp(common_list[index].perm_list[i].name, name)) {
				perm = 1U << (common_list[index].perm_list[i].datums - 1);
				break;
			}
		}
	}

	return perm;
}

static bool pp_find_common(policy_db_t *db, pp_avc_desc_t *desc, size_t *index)
{
	size_t i;
	uint32_t j;
	policy_common_t *common_list = (policy_common_t *)db->symtab_common.attr_list;
	policy_classes_t *classes_list = (policy_classes_t *)db->symtab_classes.attr_list;
	bool rc = false;

	if (!pp_find_class(db, desc, &i)) {
		return false;
	}

	for (j = 0; j < db->symtab_common.element_num; ++j) {
		if (!strcmp(common_list[j].name, classes_list[i].common_name)) {
			*index = j;
			rc = true;
			break;
		}
	}

	return rc;
}

static bool pp_find_class(policy_db_t *db, pp_avc_desc_t *desc, size_t *index)
{
	uint32_t i;
	policy_classes_t *list = (policy_classes_t *)db->symtab_classes.attr_list;
	bool rc = false;

	for (i = 0; i < db->symtab_classes.element_num; ++i) {
		if (!strcmp(list[i].name, desc->cname)) {
			*index = i;
			rc = true;
			break;
		}
	}

	return rc;
}

static bool pp_find_avc(policy_db_t *db, pp_avc_desc_t *desc, size_t *index)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;
	const char *sname = NULL;
	const char *tname = NULL;
	const char *cname = NULL;
	uint16_t specified;
	bool rc = false;

	if (pp_perf_mode) {
		for (i = 0; i < db->avtab.element_num; ++i) {
			if ((list[i].source_type == desc->sdatums)
					&& (list[i].target_type == desc->tdatums)
					&& (list[i].target_class == desc->cdatums)
					&& (list[i].specified & desc->specified)) {
				*index = i;
				rc = true;
				break;
			}
		}
	} else {
		for (i = 0; i < db->avtab.element_num; ++i) {
			sname = pp_type_to_name(db, list[i].source_type);
			tname = pp_type_to_name(db, list[i].target_type);
			cname = pp_class_to_name(db, list[i].target_class);
			specified = list[i].specified;

			if (!sname || !tname || !cname) {
				continue;
			}

			if (!strcmp(sname, desc->sname)
					&& !strcmp(tname, desc->tname)
					&& !strcmp(cname, desc->cname)
					&& (specified & desc->specified)) {
				*index = i;
				rc = true;
				break;
			}
		}
	}

	return rc;
}

static bool pp_match_permissive(policy_db_t *db, pp_perm_desc_t *desc, size_t *index)
{
	uint32_t i, j;
	policy_bitmap_t map = db->permissive_map;
	uint32_t start_bit;
	const char *name = NULL;
	bool rc = false;

	for (i = 0; i < map.node_count; ++i) {
		start_bit = map.node_list[i].start_bit;

		for (j = start_bit; j < map.high_bit; ++j) {
			if (map.node_list[i].node_map & (PP_MAPBIT << (j - start_bit))) {
				name = pp_type_to_name(db, j);
				if (name && !strcmp(name, desc->name)) {
					*index = i;
					rc = true;
					break;
				}
			}
		}
	}

	return rc;
}

static bool __attribute__ ((unused)) pp_match_avc(policy_db_t *db, pp_avc_desc_t *desc, size_t *index)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;
	const char *sname = NULL;
	const char *tname = NULL;
	const char *cname = NULL;
	uint16_t specified;
	uint32_t datums;
	bool rc = false;

	for (i = 0; i < db->avtab.element_num; ++i) {
		sname = pp_type_to_name(db, list[i].source_type);
		tname = pp_type_to_name(db, list[i].target_type);
		cname = pp_class_to_name(db, list[i].target_class);
		specified = list[i].specified;
		datums = list[i].datums;

		if (!sname || !tname || !cname) {
			continue;
		}

		if (!strcmp(sname, desc->sname)
				&& !strcmp(tname, desc->tname)
				&& !strcmp(cname, desc->cname)
				&& (specified == desc->specified)
				&& (datums & pp_name_to_perm(db, desc, desc->pname))) {
			*index = i;
			rc = true;
			break;
		}
	}

	return rc;
}

static bool pp_check_context(policy_db_t *db, pp_avc_desc_t *desc)
{
	if (pp_perf_mode) {
		if (!desc->sdatums || !desc->tdatums || !desc->cdatums) {
			return false;
		}
	} else {
		if (!pp_name_to_type(db, desc->sname)
				|| !pp_name_to_type(db, desc->tname)
				|| !pp_name_to_class(db, desc->cname)) {
			return false;
		}
	}

	return true;
}

static bool pp_check_datums(policy_db_t *db, size_t index, pp_avc_desc_t *desc)
{
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;

	if (list[index].datums == 0) {
		return false;
	}

	return true;
}

static int pp_new_permissive(policy_db_t *db, pp_perm_desc_t *desc, size_t *len)
{
	policy_bitmap_t map = db->permissive_map;
	uint32_t map_size = map.map_size;
	uint32_t node_count = map.node_count;
	policy_node_t *list = map.node_list, *ptr = NULL;
	uint32_t type = pp_name_to_type(db, desc->name);
	uint32_t start_bit = type & ~(map_size - 1);

	/* TODO: FIXME */
	if (node_count != 0 || list) {
		pr_warn("SELinux: FIXME: node count %d NOT supported in permissive map\n", node_count);
		*len = 0;
		return 0;
	}

	node_count++;
	ptr = pp_zalloc(node_count * sizeof(policy_node_t));
	if (!ptr) {
		pr_err("SELinux: unable to reallocate memory for policydb "
					"node list of length %u\n", (uint32_t)(node_count * sizeof(policy_node_t)));
		return -ENOMEM;
	}
	memcpy(ptr, list, (node_count - 1) * sizeof(policy_node_t));
	pp_free(list);
	list = ptr;

	list[node_count - 1].start_bit = start_bit;
	list[node_count - 1].node_map = PP_MAPBIT << (type - start_bit);

	db->permissive_map.high_bit = start_bit + map_size;
	db->permissive_map.node_count = node_count;
	db->permissive_map.node_list = list;

	*len = sizeof(policy_node_t);

	return 0;
}

static int pp_new_avc(policy_db_t *db, pp_avc_desc_t *desc, size_t *len)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list, *ptr = NULL;
	uint32_t num = db->avtab.element_num;
	uint32_t av;
	char *buf = NULL;
	char *orig_bufp = NULL;
	const char *pname = NULL;
	int rc;

	num++;
	ptr = pp_zalloc(num * sizeof(policy_avtab_item_t));
	if (!ptr) {
		pr_err("SELinux: unable to reallocate memory for policydb "
					"attribute list of length %u\n", (uint32_t)(num * sizeof(policy_avtab_item_t)));
		return -ENOMEM;
	}
	memcpy(ptr, list, (num - 1) * sizeof(policy_avtab_item_t));
	pp_free(list);
	list = ptr;

	if (pp_perf_mode) {
		list[num - 1].source_type = desc->sdatums;
		list[num - 1].target_type = desc->tdatums;
		list[num - 1].target_class = desc->cdatums;
	} else {
		list[num - 1].source_type = pp_name_to_type(db, desc->sname);
		list[num - 1].target_type = pp_name_to_type(db, desc->tname);
		list[num - 1].target_class = pp_name_to_class(db, desc->cname);
	}

	list[num - 1].specified = desc->specified;

	orig_bufp = buf = pp_zalloc(strlen(desc->pname) + 1);
	if (!buf) {
		pr_err("SELinux: failed to allocate memory\n");
		rc = -ENOMEM;
		goto pp_new_avc_fail;
	}
	memcpy(buf, desc->pname, strlen(desc->pname));

	for (i = 0; i < PP_DATUMS_NUM; ++i) {
		pname = strsep(&buf, PP_PERMS_DELIM);
		if (!pname || !*pname) {
			break;
		}

		if (pp_perf_mode) {
			av = 1U << (desc->pdatums[i] - 1);
		} else {
			av = pp_name_to_perm(db, desc, pname);
		}

		list[num - 1].datums |= av;
	}

	db->avtab.element_num = num;
	db->avtab.attr_list = list;

	*len = sizeof(policy_avtab_item_t);

	if (orig_bufp) {
		pp_free(orig_bufp);
		orig_bufp = NULL;
		buf = NULL;
	}

	return 0;

pp_new_avc_fail:

	if (ptr) {
		pp_free(ptr);
		ptr = NULL;
	}

	return rc;
}

static int pp_del_permissive(policy_db_t *db, size_t index, pp_perm_desc_t *desc, size_t *len)
{
	policy_bitmap_t map = db->permissive_map;
	uint32_t node_count = map.node_count;
	policy_node_t *list = map.node_list;

	/* TODO: FIXME */
	if (node_count != 1) {
		pr_warn("SELinux: FIXME: node count %d NOT supported in permissive map\n", node_count);
		*len = 0;
		return 0;
	}

	if (list) {
		pp_free(list);
		list = NULL;
	}

	db->permissive_map.high_bit = 0;
	db->permissive_map.node_count = 0;
	db->permissive_map.node_list = list;

	*len = sizeof(policy_node_t);

	return 0;
}

static int pp_del_avc(policy_db_t *db, size_t index, pp_avc_desc_t *desc, size_t *len)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;
	uint32_t num = db->avtab.element_num;

	if (num == 0) {
		*len = 0;
		return 0;
	}

	num--;

	if (num > 0) {
		for (i = index; i < num; ++i) {
			list[i] = list[i + 1];
		}
	}

	db->avtab.element_num = num;

	*len = sizeof(policy_avtab_item_t);

	return 0;
}

static int pp_mod_avc(policy_db_t *db, size_t index, pp_avc_desc_t *desc)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;
	uint32_t av;
	char *buf = NULL;
	char *orig_bufp = NULL;
	const char *pname = NULL;

	orig_bufp = buf = pp_zalloc(strlen(desc->pname) + 1);
	if (!buf) {
		pr_err("SELinux: failed to allocate memory\n");
		return -ENOMEM;
	}
	memcpy(buf, desc->pname, strlen(desc->pname));

	for (i = 0; i < PP_DATUMS_NUM; ++i) {
		pname = strsep(&buf, PP_PERMS_DELIM);
		if (!pname || !*pname) {
			break;
		}

		if (pp_perf_mode) {
			av = 1U << (desc->pdatums[i] - 1);
		} else {
			av = pp_name_to_perm(db, desc, pname);
		}

		list[index].datums |= av;
	}

	if (orig_bufp) {
		pp_free(orig_bufp);
		orig_bufp = NULL;
		buf = NULL;
	}

	return 0;
}

static int pp_unmod_avc(policy_db_t *db, size_t index, pp_avc_desc_t *desc)
{
	uint32_t i;
	policy_avtab_item_t *list = (policy_avtab_item_t *)db->avtab.attr_list;
	uint32_t av;
	char *buf = NULL;
	char *orig_bufp = NULL;
	const char *pname = NULL;

	orig_bufp = buf = pp_zalloc(strlen(desc->pname) + 1);
	if (!buf) {
		pr_err("SELinux: failed to allocate memory\n");
		return -ENOMEM;
	}
	memcpy(buf, desc->pname, strlen(desc->pname));

	for (i = 0; i < PP_DATUMS_NUM; ++i) {
		pname = strsep(&buf, PP_PERMS_DELIM);
		if (!pname || !*pname) {
			break;
		}

		if (pp_perf_mode) {
			av = 1U << (desc->pdatums[i] - 1);
		} else {
			av = pp_name_to_perm(db, desc, pname);
		}

		list[index].datums &= ~av;
	}

	if (orig_bufp) {
		pp_free(orig_bufp);
		orig_bufp = NULL;
		buf = NULL;
	}

	return 0;
}

static int pp_preproc_permissive(policy_db_t *db, pp_perm_desc_t *list, size_t list_len, size_t *perm_len)
{
	uint32_t i;
	size_t index;
	size_t len;
	int rc = 0;

	for (i = 0; i < list_len; ++i) {
		if (pp_match_permissive(db, &list[i], &index)) {
			continue;
		}

		rc = pp_new_permissive(db, &list[i], &len);
		if (rc) {
			break;
		}
		*perm_len += len;
	}

	return rc;
}

static int pp_preproc_avc(policy_db_t *db, pp_avc_desc_t *list, size_t list_len, size_t *avc_len)
{
	uint32_t i;
	size_t index;
	size_t len;
	int rc = 0;

	for (i = 0; i < list_len; ++i) {
		if (pp_find_avc(db, &list[i], &index)) {
			rc = pp_mod_avc(db, index, &list[i]);
			if (rc) {
				break;
			}
		} else {
			if (pp_check_context(db, &list[i])) {
				rc = pp_new_avc(db, &list[i], &len);
				if (rc) {
					break;
				}
				*avc_len += len;
			}
		}
	}

	return rc;
}

static int pp_postproc_permissive(policy_db_t *db, pp_perm_desc_t *list, size_t list_len, size_t *perm_len)
{
	uint32_t i;
	size_t index;
	size_t len;
	int rc = 0;

	for (i = 0; i < list_len; ++i) {
		if (!pp_match_permissive(db, &list[i], &index)) {
			continue;
		}

		rc = pp_del_permissive(db, index, &list[i], &len);
		if (rc) {
			break;
		}
		*perm_len += len;
	}

	return rc;
}

static int pp_postproc_avc(policy_db_t *db, pp_avc_desc_t *list, size_t list_len, size_t *avc_len)
{
	uint32_t i;
	size_t index;
	size_t len;
	int rc = 0;

	for (i = 0; i < list_len; ++i) {
		if (!pp_find_avc(db, &list[i], &index)) {
			continue;
		}

		rc = pp_unmod_avc(db, index, &list[i]);
		if (rc) {
			break;
		}

		if (!pp_check_datums(db, index, &list[i])) {
			rc = pp_del_avc(db, index, &list[i], &len);
			if (rc) {
				break;
			}
			*avc_len += len;
		}
	}

	return rc;
}

/**
 * pp_preproc_policy - preprocess policy as binary image
 * @data: policy as binary image
 * @len: policy size
 *
 * This function preprocesses policy as binary image, e.g,
 * insert policy instructions into it.
 */
int pp_preproc_policy(void **data, size_t *len)
{
	struct policy_file file = { *data, *len };
	policy_db_t policy_db = { 0 };
	size_t file_len = file.len, perm_len = 0, avc_len = 0;
	char *ptr = NULL;
	int rc;

	pr_info("%s: e\n", __func__);

	rc = pp_parse_policy(&file, &policy_db);
	if (rc) {
		pr_err("SELinux: failed to parse policy\n");
		if (rc == WARNING_HEADER_UNSUPPORTED) {
			rc = 0;
		}
		goto pp_preproc_policy_exit;
	}

	rc = pp_parse_appendix(&file, &policy_db.appx);
	if (!rc) {
		rc = pp_appendix_to_avc(&policy_db.appx, pp_avc_desc_list, ARRAY_SIZE(pp_avc_desc_list));
		if (rc) {
			pr_err("SELinux: failed to update avc with appendix\n");
			goto pp_preproc_policy_exit;
		}

		pr_info("%s: appendix found, switch to perf mode\n", __func__);
		pp_perf_mode = 1;
	}

	rc = pp_preproc_permissive(&policy_db, pp_perm_desc_list, ARRAY_SIZE(pp_perm_desc_list), &perm_len);
	if (rc) {
		pr_err("SELinux: failed to insert permissive policy\n");
		goto pp_preproc_policy_exit;
	}
	file_len += perm_len;

	rc = pp_preproc_avc(&policy_db, pp_avc_desc_list, ARRAY_SIZE(pp_avc_desc_list), &avc_len);
	if (rc) {
		pr_err("SELinux: failed to insert avc policy\n");
		goto pp_preproc_policy_exit;
	}
	file_len += avc_len;

	ptr = vzalloc(file_len);
	if (!ptr) {
		pr_err("SELinux: failed to allocate policy\n");
		goto pp_preproc_policy_exit;
	}
	file.data = ptr;
	file.len = file_len;

	rc = pp_build_policy(&policy_db, &file);
	if (rc) {
		pr_err("SELinux: failed to build policy\n");
		goto pp_preproc_policy_exit;
	}

	*len = file_len;

	vfree(*data);
	*data = vzalloc(*len);
	if (!*data) {
		pr_err("SELinux: failed to allocate policy\n");
		rc = -ENOMEM;
		goto pp_preproc_policy_exit;
	}
	memcpy(*data, ptr, *len);

/*
 * Refer to /sys/kernel/debug/preproc_policy
 * Run: adb shell "cat /sys/kernel/debug/preproc_policy > /data/policy"
 */
#if defined(PP_DEBUGFS)
	pp_debugfs_preproc_file.len = file_len;

	pp_debugfs_preproc_file.data = vzalloc(pp_debugfs_preproc_file.len);
	memcpy(pp_debugfs_preproc_file.data, ptr, pp_debugfs_preproc_file.len);

	(void *)debugfs_create_file("preproc_policy", 0664,
		NULL, NULL, &pp_debugfs_preproc_fops);
#endif /* PP_DEBUGFS */

	rc = 0;

	pr_info("%s: x\n", __func__);

pp_preproc_policy_exit:

	if (ptr) {
		vfree(ptr);
	}

	(void)pp_free_appendix(&policy_db.appx);
	(void)pp_free_policy(&policy_db);

	return rc;
}
EXPORT_SYMBOL(pp_preproc_policy);

/**
 * pp_postproc_policy - postprocess policy as binary image
 * @data: policy as binary image
 * @len: policy size
 *
 * This function postprocesses policy as binary image, e.g,
 * remove policy instructions from it.
 */
int pp_postproc_policy(void **data, size_t *len)
{
	struct policy_file file = { *data, *len };
	policy_db_t policy_db = { 0 };
	size_t file_len = file.len, perm_len = 0, avc_len = 0;
	char *ptr = NULL;
	int rc;

	pr_info("%s: e\n", __func__);

	rc = pp_parse_policy(&file, &policy_db);
	if (rc) {
		pr_err("SELinux: failed to parse policy\n");
		if (rc == WARNING_HEADER_UNSUPPORTED) {
			rc = 0;
		}
		goto pp_postproc_policy_exit;
	}

	rc = pp_postproc_permissive(&policy_db, pp_perm_desc_list, ARRAY_SIZE(pp_perm_desc_list), &perm_len);
	if (rc) {
		pr_err("SELinux: failed to remove permissive policy\n");
		goto pp_postproc_policy_exit;
	}

	rc = pp_postproc_avc(&policy_db, pp_avc_desc_list, ARRAY_SIZE(pp_avc_desc_list), &avc_len);
	if (rc) {
		pr_err("SELinux: failed to remove avc policy\n");
		goto pp_postproc_policy_exit;
	}

	ptr = vzalloc(file_len);
	if (!ptr) {
		pr_err("SELinux: failed to allocate policy\n");
		goto pp_postproc_policy_exit;
	}
	file.data = ptr;
	file.len = file_len;

	rc = pp_build_policy(&policy_db, &file);
	if (rc) {
		pr_err("SELinux: failed to build policy\n");
		goto pp_postproc_policy_exit;
	}

	*len = file_len;

	vfree(*data);
	*data = vzalloc(*len);
	if (!*data) {
		pr_err("SELinux: failed to allocate policy\n");
		rc = -ENOMEM;
		goto pp_postproc_policy_exit;
	}
	memcpy(*data, ptr, *len);

/*
 * Refer to /sys/kernel/debug/postproc_policy
 * Run: adb shell "cat /sys/kernel/debug/postproc_policy > /data/policy"
 */
#if defined(PP_DEBUGFS)
	pp_debugfs_postproc_file.len = file_len;

	pp_debugfs_postproc_file.data = vzalloc(pp_debugfs_postproc_file.len);
	memcpy(pp_debugfs_postproc_file.data, ptr, pp_debugfs_postproc_file.len);

	(void *)debugfs_create_file("postproc_policy", 0664,
		NULL, NULL, &pp_debugfs_postproc_fops);
#endif /* PP_DEBUGFS */

	rc = 0;

	pr_info("%s: x\n", __func__);

pp_postproc_policy_exit:

	if (ptr) {
		vfree(ptr);
	}

	(void)pp_free_policy(&policy_db);

	return rc;
}
EXPORT_SYMBOL(pp_postproc_policy);

/**
 * pp_postproc_av_perms - postprocess av permissions
 * @pol: policy db
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @perms: permissions
 *
 * This function postprocesses av permissions.
 */
int pp_postproc_av_perms(struct policydb *pol, u32 ssid, u32 tsid, u16 tclass, u32 *perms)
{
	uint32_t i;
	pp_avc_desc_t *list = &pp_avc_desc_list[AVC_ADBD_KERNEL];
	size_t scon_len = strlen("u:r::s0") + strlen(list->sname) + 1;
	size_t tcon_len = strlen("u:object_r::s0") + strlen(list->tname) + 1;
	char scon[scon_len], tcon[tcon_len], pname[strlen(list->pname) + 1];
	u32 sid = 0, perm = 0;
	u16 class = 0;
	const char *ptr = NULL, *str = NULL;
	int rc;

	memset(scon, 0, scon_len);
	(void)snprintf(scon, scon_len, "u:r:%s:s0", list->sname);

#if KERNEL_VERSION(3, 14, 0) <= LINUX_VERSION_CODE
	rc = security_context_to_sid(scon, scon_len, &sid, GFP_ATOMIC);
#else
	rc = security_context_to_sid(scon, scon_len, &sid);
#endif /* LINUX_VERSION_CODE */

	if (rc || (sid != ssid)) {
		goto pp_postproc_av_perms_exit;
	}

	memset(tcon, 0, tcon_len);
	(void)snprintf(tcon, tcon_len, "u:object_r:%s:s0", list->tname);

#if KERNEL_VERSION(3, 14, 0) <= LINUX_VERSION_CODE
	rc = security_context_to_sid(tcon, tcon_len, &sid, GFP_ATOMIC);
#else
	rc = security_context_to_sid(tcon, tcon_len, &sid);
#endif /* LINUX_VERSION_CODE */

	if (rc || (sid != tsid)) {
		goto pp_postproc_av_perms_exit;
	}

	class = string_to_security_class(pol, list->cname);
	if (class != tclass) {
		goto pp_postproc_av_perms_exit;
	}

	memset(pname, 0, sizeof(pname));
	memcpy(pname, list->pname, strlen(list->pname));

	ptr = pname;
	for (i = 0; i < PP_DATUMS_NUM; ++i) {
		str = strsep((char **)&ptr, PP_PERMS_DELIM);
		if (!str || !*str) {
			break;
		}

		perm = string_to_av_perm(pol, class, str);
		*perms &= ~perm;
	}

pp_postproc_av_perms_exit:

	return 0;
}
EXPORT_SYMBOL(pp_postproc_av_perms);
