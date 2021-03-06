/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2017 Red Hat, Inc.
 */

#ifndef __NM_DEDUP_MULTI_H__
#define __NM_DEDUP_MULTI_H__

#include "nm-obj.h"
#include "c-list.h"

/*****************************************************************************/

typedef struct _NMDedupMultiObj             NMDedupMultiObj;
typedef struct _NMDedupMultiObjClass        NMDedupMultiObjClass;
typedef struct _NMDedupMultiIdxType         NMDedupMultiIdxType;
typedef struct _NMDedupMultiIdxTypeClass    NMDedupMultiIdxTypeClass;
typedef struct _NMDedupMultiEntry           NMDedupMultiEntry;
typedef struct _NMDedupMultiHeadEntry       NMDedupMultiHeadEntry;
typedef struct _NMDedupMultiIndex           NMDedupMultiIndex;

typedef enum _NMDedupMultiIdxMode {
	NM_DEDUP_MULTI_IDX_MODE_PREPEND,

	NM_DEDUP_MULTI_IDX_MODE_PREPEND_FORCE,

	/* append new objects to the end of the list.
	 * If the object is already in the cache, don't move it. */
	NM_DEDUP_MULTI_IDX_MODE_APPEND,

	/* like NM_DEDUP_MULTI_IDX_MODE_APPEND, but if the object
	 * is already in teh cache, move it to the end. */
	NM_DEDUP_MULTI_IDX_MODE_APPEND_FORCE,
} NMDedupMultiIdxMode;

/*****************************************************************************/

struct _NMDedupMultiObj {
	union {
		NMObjBaseInst parent;
		const NMDedupMultiObjClass *klass;
	};
	NMDedupMultiIndex *_multi_idx;
	guint _ref_count;
};

struct _NMDedupMultiObjClass {
	NMObjBaseClass parent;

	const NMDedupMultiObj *(*obj_clone) (const NMDedupMultiObj *obj);

	gboolean (*obj_needs_clone) (const NMDedupMultiObj *obj);

	void (*obj_destroy) (NMDedupMultiObj *obj);

	/* the NMDedupMultiObj can be deduplicated. For that the obj_full_hash()
	 * and obj_full_equal() compare *all* fields of the object, even minor ones. */
	guint    (*obj_full_hash)  (const NMDedupMultiObj *obj);
	gboolean (*obj_full_equal) (const NMDedupMultiObj *obj_a,
	                            const NMDedupMultiObj *obj_b);
};

/*****************************************************************************/

const NMDedupMultiObj *nm_dedup_multi_obj_ref         (const NMDedupMultiObj *obj);
const NMDedupMultiObj *nm_dedup_multi_obj_unref       (const NMDedupMultiObj *obj);
const NMDedupMultiObj *nm_dedup_multi_obj_clone       (const NMDedupMultiObj *obj);
gboolean               nm_dedup_multi_obj_needs_clone (const NMDedupMultiObj *obj);

gconstpointer nm_dedup_multi_index_obj_intern (NMDedupMultiIndex *self,
                                               /* const NMDedupMultiObj * */ gconstpointer obj);

void nm_dedup_multi_index_obj_release (NMDedupMultiIndex *self,
                                       /* const NMDedupMultiObj * */ gconstpointer obj);

/* const NMDedupMultiObj * */ gconstpointer nm_dedup_multi_index_obj_find (NMDedupMultiIndex *self,
                                                                           /* const NMDedupMultiObj * */ gconstpointer obj);

/*****************************************************************************/

/* the NMDedupMultiIdxType is an access handle under which you can store and
 * retrieve NMDedupMultiObj instances in NMDedupMultiIndex.
 *
 * The NMDedupMultiIdxTypeClass determines it's behavior, but you can have
 * multiple instances (of the same class).
 *
 * For example, NMIP4Config can have idx-type to put there all IPv4 Routes.
 * This idx-type instance is private to the NMIP4Config instance. Basically,
 * the NMIP4Config instance uses the idx-type to maintain an ordered list
 * of routes in NMDedupMultiIndex.
 *
 * However, a NMDedupMultiIdxType may also partition the set of objects
 * in multiple distinct lists. NMIP4Config doesn't do that (because instead
 * of creating one idx-type for IPv4 and IPv6 routes, it just cretaes
 * to distinct idx-types, one for each address family.
 * This partitioning is used by NMPlatform to maintain a lookup index for
 * routes by ifindex. As the ifindex is dynamic, it does not create an
 * idx-type instance for each ifindex. Instead, it has one idx-type for
 * all routes. But whenever accessing NMDedupMultiIndex with an NMDedupMultiObj,
 * the partitioning NMDedupMultiIdxType takes into accound the NMDedupMultiObj
 * instance to associate it with the right list.
 *
 * Hence, a NMDedupMultiIdxEntry has a list of possibly multiple NMDedupMultiHeadEntry
 * instances, which each is the head for a list of NMDedupMultiEntry instances.
 * In the platform example, the NMDedupMultiHeadEntry parition the indexed objects
 * by their ifindex. */
struct _NMDedupMultiIdxType {
	union {
		NMObjBaseInst parent;
		const NMDedupMultiIdxTypeClass *klass;
	};

	CList lst_idx_head;

	guint len;
};

void nm_dedup_multi_idx_type_init (NMDedupMultiIdxType *idx_type,
                                   const NMDedupMultiIdxTypeClass *klass);

struct _NMDedupMultiIdxTypeClass {
	NMObjBaseClass parent;

	guint    (*idx_obj_id_hash)  (const NMDedupMultiIdxType *idx_type,
	                              const NMDedupMultiObj *obj);
	gboolean (*idx_obj_id_equal) (const NMDedupMultiIdxType *idx_type,
	                              const NMDedupMultiObj *obj_a,
	                              const NMDedupMultiObj *obj_b);

	/* an NMDedupMultiIdxTypeClass which implements partitioning of the
	 * tracked objects, must implement the idx_obj_partition*() functions.
	 *
	 * idx_obj_partitionable() may return NULL if the object cannot be tracked.
	 * For example, a index for routes by ifindex, may not want to track any
	 * routes that don't have a valid ifindex. If the idx-type says that the
	 * object is not partitionable, it is never added to the NMDedupMultiIndex. */
	gboolean (*idx_obj_partitionable)   (const NMDedupMultiIdxType *idx_type,
	                                     const NMDedupMultiObj *obj);
	guint    (*idx_obj_partition_hash)  (const NMDedupMultiIdxType *idx_type,
	                                     const NMDedupMultiObj *obj);
	gboolean (*idx_obj_partition_equal) (const NMDedupMultiIdxType *idx_type,
	                                     const NMDedupMultiObj *obj_a,
	                                     const NMDedupMultiObj *obj_b);
};

static inline gboolean
nm_dedup_multi_idx_type_id_equal (const NMDedupMultiIdxType *idx_type,
                                  /* const NMDedupMultiObj * */ gconstpointer obj_a,
                                  /* const NMDedupMultiObj * */ gconstpointer obj_b)
{
	nm_assert (idx_type);
	return    obj_a == obj_b
	       || idx_type->klass->idx_obj_id_equal (idx_type,
	                                             obj_a,
	                                             obj_b);
}

static inline gboolean
nm_dedup_multi_idx_type_partition_equal (const NMDedupMultiIdxType *idx_type,
                                         /* const NMDedupMultiObj * */ gconstpointer obj_a,
                                         /* const NMDedupMultiObj * */ gconstpointer obj_b)
{
	nm_assert (idx_type);
	if (idx_type->klass->idx_obj_partition_equal) {
		nm_assert (obj_a);
		nm_assert (obj_b);
		return    obj_a == obj_b
		       || idx_type->klass->idx_obj_partition_equal (idx_type,
		                                                    obj_a,
		                                                    obj_b);
	}
	return TRUE;
}

/*****************************************************************************/

struct _NMDedupMultiEntry {

	/* this is the list of all entries that share the same head entry.
	 * All entries compare equal according to idx_obj_partition_equal(). */
	CList lst_entries;

	/* const NMDedupMultiObj * */ gconstpointer obj;

	bool is_head;
	bool dirty;

	const NMDedupMultiHeadEntry *head;
};

struct _NMDedupMultiHeadEntry {

	/* this is the list of all entries that share the same head entry.
	 * All entries compare equal according to idx_obj_partition_equal(). */
	CList lst_entries_head;

	const NMDedupMultiIdxType *idx_type;

	bool is_head;

	guint len;

	CList lst_idx;
};

static inline void
nm_dedup_multi_entry_set_dirty (const NMDedupMultiEntry *entry,
                                gboolean dirty)
{
	/* NMDedupMultiEntry is always exposed as a const object, because it is not
	 * supposed to be modified outside NMDedupMultiIndex API. Except the "dirty"
	 * flag. In C++ speak, it is a mutable field.
	 *
	 * Add this inline function, to case-away constness and set the dirty flag. */
	nm_assert (entry);
	((NMDedupMultiEntry *) entry)->dirty = dirty;
}

/*****************************************************************************/

NMDedupMultiIndex *nm_dedup_multi_index_new (void);
NMDedupMultiIndex *nm_dedup_multi_index_ref (NMDedupMultiIndex *self);
NMDedupMultiIndex *nm_dedup_multi_index_unref (NMDedupMultiIndex *self);

static inline void
_nm_auto_unref_dedup_multi_index (NMDedupMultiIndex **v)
{
	if (*v)
		nm_dedup_multi_index_unref (*v);
}
#define nm_auto_unref_dedup_multi_index nm_auto(_nm_auto_unref_dedup_multi_index)

#define NM_DEDUP_MULTI_ENTRY_MISSING      ((const NMDedupMultiEntry *)     GUINT_TO_POINTER (1))
#define NM_DEDUP_MULTI_HEAD_ENTRY_MISSING ((const NMDedupMultiHeadEntry *) GUINT_TO_POINTER (1))

gboolean nm_dedup_multi_index_add_full (NMDedupMultiIndex *self,
                                        NMDedupMultiIdxType *idx_type,
                                        /*const NMDedupMultiObj * */ gconstpointer obj,
                                        NMDedupMultiIdxMode mode,
                                        const NMDedupMultiEntry *entry_order,
                                        const NMDedupMultiEntry *entry_existing,
                                        const NMDedupMultiHeadEntry *head_existing,
                                        const NMDedupMultiEntry **out_entry,
                                        /* const NMDedupMultiObj ** */ gpointer out_obj_old);

gboolean nm_dedup_multi_index_add (NMDedupMultiIndex *self,
                                   NMDedupMultiIdxType *idx_type,
                                   /*const NMDedupMultiObj * */ gconstpointer obj,
                                   NMDedupMultiIdxMode mode,
                                   const NMDedupMultiEntry **out_entry,
                                   /* const NMDedupMultiObj ** */ gpointer out_obj_old);

const NMDedupMultiEntry *nm_dedup_multi_index_lookup_obj (NMDedupMultiIndex *self,
                                                          const NMDedupMultiIdxType *idx_type,
                                                          /*const NMDedupMultiObj * */ gconstpointer obj);

const NMDedupMultiHeadEntry *nm_dedup_multi_index_lookup_head (NMDedupMultiIndex *self,
                                                               const NMDedupMultiIdxType *idx_type,
                                                               /*const NMDedupMultiObj * */ gconstpointer obj);

guint nm_dedup_multi_index_remove_entry (NMDedupMultiIndex *self,
                                         gconstpointer entry);

guint nm_dedup_multi_index_remove_obj (NMDedupMultiIndex *self,
                                       NMDedupMultiIdxType *idx_type,
                                       /*const NMDedupMultiObj * */ gconstpointer obj);

guint nm_dedup_multi_index_remove_head (NMDedupMultiIndex *self,
                                        NMDedupMultiIdxType *idx_type,
                                        /*const NMDedupMultiObj * */ gconstpointer obj);

guint nm_dedup_multi_index_remove_idx (NMDedupMultiIndex *self,
                                       NMDedupMultiIdxType *idx_type);

void nm_dedup_multi_index_dirty_set_head (NMDedupMultiIndex *self,
                                          const NMDedupMultiIdxType *idx_type,
                                          /*const NMDedupMultiObj * */ gconstpointer obj);

void nm_dedup_multi_index_dirty_set_idx (NMDedupMultiIndex *self,
                                         const NMDedupMultiIdxType *idx_type);

guint nm_dedup_multi_index_dirty_remove_idx (NMDedupMultiIndex *self,
                                             NMDedupMultiIdxType *idx_type,
                                             gboolean mark_survivors_dirty);

/*****************************************************************************/

typedef struct _NMDedupMultiIter {
	const NMDedupMultiHeadEntry *head;
	const NMDedupMultiEntry *current;
	const NMDedupMultiEntry *next;
} NMDedupMultiIter;

static inline void
nm_dedup_multi_iter_init (NMDedupMultiIter *iter, const NMDedupMultiHeadEntry *head)
{
	g_return_if_fail (iter);

	iter->head = head;
	iter->current = NULL;
	iter->next = head && !c_list_is_empty (&head->lst_entries_head)
	             ? c_list_entry (head->lst_entries_head.next, NMDedupMultiEntry, lst_entries)
	             : NULL;
}

static inline gboolean
nm_dedup_multi_iter_next (NMDedupMultiIter *iter)
{
	g_return_val_if_fail (iter, FALSE);

	if (!iter->next)
		return FALSE;

	/* we always look ahead for the @next. This way, the user
	 * may delete the current entry (but no other entries). */
	iter->current = iter->next;
	if (iter->next->lst_entries.next == &iter->head->lst_entries_head)
		iter->next = NULL;
	else
		iter->next = c_list_entry (iter->next->lst_entries.next, NMDedupMultiEntry, lst_entries);
	return TRUE;
}

static inline void
nm_dedup_multi_iter_rewind (NMDedupMultiIter *iter)
{
	/* rewind the iterator.
	 *
	 * In principle, you can always delete the current entry.
	 * However, if you delete *all* current entries, the list
	 * head becomes invalid too and rewinding will crash.
	 *
	 * So, either
	 *  - don't modify the list
	 *  - if you modify it:
	 *    - only delete the current entry, don't delete other entries.
	 *    - you may add more entries, however that may make iteration
	 *      confusing.
	 *  - you may rewind the iterator, but only if not all
	 *    entires were deleted.
	 *
	 * Use with care. */
	g_return_if_fail (iter);
	nm_dedup_multi_iter_init (iter, iter->head);
}

#define nm_dedup_multi_iter_for_each(iter, head_entry) \
	for (nm_dedup_multi_iter_init ((iter), (head_entry)); \
	     nm_dedup_multi_iter_next ((iter)); \
	     )

/*****************************************************************************/

typedef gboolean (*NMDedupMultiFcnSelectPredicate) (/* const NMDedupMultiObj * */ gconstpointer obj,
                                                    gpointer user_data);

GPtrArray *nm_dedup_multi_objs_to_ptr_array_head (const NMDedupMultiHeadEntry *head_entry,
                                                  NMDedupMultiFcnSelectPredicate predicate,
                                                  gpointer user_data);

/*****************************************************************************/

#endif /* __NM_DEDUP_MULTI_H__ */
