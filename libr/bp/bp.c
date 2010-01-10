/* radare - LGPL - Copyright 2009 pancake<nopcode.org> */

#include <r_bp.h>
#include "../config.h"

static struct r_bp_handle_t *bp_static_plugins[] = 
	{ R_BP_STATIC_PLUGINS };

R_API int r_bp_init(struct r_bp_t *bp)
{
	int i;
	bp->nbps = 0;
	bp->cur = NULL;
	bp->stepcont = R_BP_CONT_NORMAL;
	INIT_LIST_HEAD(&bp->bps);
	INIT_LIST_HEAD(&bp->plugins);
	for(i=0;bp_static_plugins[i];i++)
		r_bp_handle_add(bp, bp_static_plugins[i]);
	memset (&bp->iob, 0, sizeof(bp->iob));
	return R_TRUE;
}

R_API struct r_bp_t *r_bp_new()
{
	struct r_bp_t *bp = MALLOC_STRUCT(struct r_bp_t);
	if (bp) r_bp_init(bp);
	return bp;
}

R_API struct r_bp_t *r_bp_free(struct r_bp_t *bp)
{
	/* XXX : properly destroy bp list */
	free(bp);
	return NULL;
}

R_API int r_bp_get_bytes(struct r_bp_t *bp, ut8 *buf, int len, int endian, int idx)
{
	int i;
	struct r_bp_arch_t *b;
	if (bp->cur) {
		/* XXX: can be buggy huh : infinite loop is possible */
		for(i=0;1;i++) {
			b = &bp->cur->bps[i%bp->cur->nbps];
			if (b->endian == endian && idx%(i+1)==0) {
				for(i=0;i<len;) {
					memcpy(buf+i, b->bytes, len);
					i += b->length;
				}
				return R_TRUE;
			}
		}
	}
	return R_FALSE;
}

R_API int r_bp_at_addr(struct r_bp_t *bp, ut64 addr, int rwx)
{
	struct list_head *pos;
	struct r_bp_item_t *b;

	if (bp->trace_bp == addr)
		return R_TRUE;

	list_for_each(pos, &bp->bps) {
		b = list_entry(pos, struct r_bp_item_t, list);
		if (addr >= b->addr && addr <= b->addr+b->size && rwx&b->rwx)
			return R_TRUE;
	}
	return R_FALSE;
}

R_API struct r_bp_item_t *r_bp_enable(struct r_bp_t *bp, ut64 addr, int set)
{
	struct list_head *pos;
	struct r_bp_item_t *b;
	list_for_each(pos, &bp->bps) {
		b = list_entry(pos, struct r_bp_item_t, list);
		if (addr >= b->addr && addr <= b->addr+b->size) {
			b->enabled = set;
			return b;
		}
	}
	return NULL;
}

R_API int r_bp_stepy_continuation(struct r_bp_t *bp)
{
	// TODO: implement
	return bp->stepcont;
}

R_API int r_bp_add_cond(struct r_bp_t *bp, const char *cond)
{
	// TODO: implement contitional breakpoints
	bp->stepcont = R_TRUE;
	return 0;
}

R_API int r_bp_del_cond(struct r_bp_t *bp, int idx)
{
	// add contitional
	bp->stepcont = R_FALSE;
	return R_TRUE;
}

/* TODO: detect overlapping of breakpoints */
static struct r_bp_item_t *r_bp_add(struct r_bp_t *bp, const ut8 *obytes, ut64 addr, int size, int hw, int rwx)
{
	int ret;
	struct r_bp_item_t *b;
	if (r_bp_at_addr(bp, addr, rwx)) {
		eprintf("Breakpoint already set at this address.\n");
		return NULL;
	}
	b = MALLOC_STRUCT(struct r_bp_item_t);
	b->pids[0] = 0; /* for any pid */
	b->addr = addr;
	b->size = size;
	b->enabled = R_TRUE;
	b->bbytes = malloc(size+16);
	if (obytes) {
		b->obytes = malloc(size);
		memcpy(b->obytes, obytes, size);
	} else b->obytes = NULL;
	/* XXX: endian always in little ?!?!? */
	ret = r_bp_get_bytes(bp, b->bbytes, size, 0, 0);
	if (ret == R_FALSE) {
		fprintf(stderr, "Cannot get breakpoint bytes. No r_bp_use()?\n");
		free (b->bbytes);
		free (b);
		return NULL;
	}
	b->hw = hw;
	b->trace = 0;
	bp->nbps++;
	list_add_tail(&(b->list), &bp->bps);
	return b;
}

R_API int r_bp_add_fault(struct r_bp_t *bp, ut64 addr, int size, int rwx)
{
	// TODO
	return R_FALSE;
}

R_API struct r_bp_item_t *r_bp_add_sw(struct r_bp_t *bp, ut64 addr, int size, int rwx)
{
	struct r_bp_item_t *item;
	ut8 *bytes;
	bytes = malloc(size);
	if (bytes == NULL)
		return NULL;
	if (bp->iob.read_at) {
		bp->iob.read_at(bp->iob.io, addr, bytes, size);
	} else memset(bytes, 0, size);
	item = r_bp_add(bp, bytes, addr, size, R_BP_TYPE_SW, rwx);
	free(bytes);
	return item;
}

R_API struct r_bp_item_t *r_bp_add_hw(struct r_bp_t *bp, ut64 addr, int size, int rwx)
{
	return r_bp_add(bp, NULL, addr, size, R_BP_TYPE_HW, rwx);
}

R_API int r_bp_del(struct r_bp_t *bp, ut64 addr)
{
	struct list_head *pos;
	struct r_bp_item_t *b;
	list_for_each(pos, &bp->bps) {
		b = list_entry(pos, struct r_bp_item_t, list);
		if (b->addr == addr) {
			list_del(&b->list);
			return R_TRUE;
		}
	}
	return R_FALSE;
}

// TODO: rename or drop?
R_API int r_bp_set_trace(struct r_bp_t *bp, ut64 addr, int set)
{
	struct list_head *pos;
	struct r_bp_item_t *b;
	list_for_each(pos, &bp->bps) {
		b = list_entry(pos, struct r_bp_item_t, list);
		if (addr >= b->addr && addr <= b->addr+b->size) {
			b->trace = set;
			return R_TRUE;
		}
	}
	return R_TRUE;
}

// TODO: rename or remove
R_API int r_bp_set_trace_bp(struct r_bp_t *bp, ut64 addr, int set)
{
	bp->trace_all = set;
	bp->trace_bp = addr;
	return R_TRUE;
}

// TODO: deprecate
R_API int r_bp_list(struct r_bp_t *bp, int rad)
{
	int n = 0;
	struct r_bp_item_t *b;
	struct list_head *pos;
	eprintf("Breakpoint list:\n");
	list_for_each(pos, &bp->bps) {
		b = list_entry(pos, struct r_bp_item_t, list);
		printf("0x%08llx - 0x%08llx %d %c%c%c %s %s %s\n",
			b->addr, b->addr+b->size, b->size,
			(b->rwx & R_BP_PROT_READ)?'r':'-',
			(b->rwx & R_BP_PROT_WRITE)?'w':'-',
			(b->rwx & R_BP_PROT_EXEC)?'x':'-',
			b->hw?"hw":"sw",
			b->trace?"trace":"break",
			b->enabled?"enabled":"disabled");
		/* TODO: Show list of pids and trace points, conditionals */
		n++;
	}
	return n;
}
