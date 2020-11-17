/*
 * Copyright (c) 2020 Ian Fitchet <idf(at)idio-lang.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * malloc.c
 *
 * This is a variation on Chris Kingsley's 1982 malloc.c of which
 * there are numerous descendents including FreeBSD's
 * libexec/rtld-elf/malloc.c and the (Emacs then) Bash derived
 * lib/malloc/malloc.c.
 *
 * There's a suggestion it might have some unique fragmentation
 * behaviour for which Doug Lea's
 * http://gee.cs.oswego.edu/dl/html/malloc.html allocator's algorithms
 * might help.
 *
 * There's a nod to Bash's reentrancy and memory guard code too.
 *
 * Unfortunately, I can't unearth a reference to the original Kingsley
 * code.
 *
 * In the meanwhile there's some unwritten nuance.  Before calling any
 * of the original code we're meant to pre-allocate some memory and
 * initialise idio_malloc_pagepool_start/end to it.  The Bash code is
 * more dynamic and uses a pagealign() function to 1) grab *some*
 * memory, up to a page boundary and then 2) create a shortened pool
 * in that.
 *
 * The Bash code continues to work with the idio_malloc_bucket_sizes[]
 * bins meaning the pagepool_start/end references can go.
 *
 * The code limits you to a 32bit allocation (4GB).  That can be
 * change with adjustments to IDIO_MALLOC_NBUCKETS and the uint32_t
 * for ovu_size.  The obvious change would be to allow 64bit
 * allocations which means the header becomes 16 bytes long (because
 * of the 8 byte ovu_size field).  That may tease out some
 * assumptions...
 */

/*
 * malloc.c (Caltech) 2/21/82
 * Chris Kingsley, kingsley@cit-20.
 *
 * This is a very fast storage allocator.  It allocates blocks of a small
 * number of different sizes, and keeps free lists of each size.  Blocks that
 * don't exactly fit are passed up to the next larger size.  In this
 * implementation, the available sizes are 2^n-4 (or 2^n-10) bytes long.
 * This is designed for use in a virtual memory environment.
 */

#include "idio.h"

#include <sys/mman.h>

/*
 * The overhead on a block is at least 8 bytes using the ov_align
 * field.  When free, this space contains a pointer to the next free
 * block, and the bottom two bits must be zero.
 *
 * When in use, the first byte is set to IDIO_MALLOC_MAGIC_ALLOC (or
 * _FREE), and the second byte is the bucket index.
 *
 * Range checking is enabled with a uint32_t holding the size of
 * the requested block.
 *
 * The order of elements is critical: ov_magic must overlay the low
 * order bits of [the pointer], and ov_magic can not be a valid
 * [pointer] bit pattern.
 */
union idio_malloc_overhead_u {
    uint64_t o_align;		/* 					8 bytes */
    struct {
	uint8_t ovu_magic;	/* magic number				1 */
	uint8_t ovu_bucket;	/* bucket #				1 */
	uint16_t ovu_rmagic;	/* range magic number			2 */
	uint32_t ovu_size;	/* actual block size			4 */
    } ovu;
#define ov_magic	ovu.ovu_magic
#define ov_bucket	ovu.ovu_bucket
#define ov_rmagic	ovu.ovu_rmagic
#define ov_size		ovu.ovu_size
};

/*
 * Instead of a single MAGIC number, Bash uses an ISFREE/ISALLOC pair
 * -- in both cases the bottom two bits cannot be 00
 */
#define IDIO_MALLOC_MAGIC_FREE	0xbf		/* f for free */
#define IDIO_MALLOC_MAGIC_ALLOC	0xbb		/* b for ... blob */
#define IDIO_MALLOC_RMAGIC	0x5555		/* magic # on range info */

/*
 * There's one ovu_rmagic field in the header and this will give us
 * room for one after the allocated block
 */
#define IDIO_MALLOC_RSLOP		sizeof (uint16_t)

/*
 * Bash enhancement:
 *
 * Access free-list pointer of a block. It is stored at block + sizeof
 * (char *). This is not a field in the ovu structure member of union
 * idio_malloc_overhead_u because we want sizeof (union
 * idio_malloc_overhead_u) to describe the overhead for when the block
 * is in use, and we do not want the free-list pointer to count in
 * that.
 */

#define IDIO_MALLOC_OVERHEAD_CHAIN(p)	(*(union idio_malloc_overhead_u **) ((char *) (p) + sizeof (char *)))

#define IDIO_MALLOC_BUCKET_RANGE(sz,b)	(((sz) > idio_malloc_bucket_sizes[(b)-1]) && ((sz) <= idio_malloc_bucket_sizes[(b)]))

static void idio_malloc_morecore (int bucket);
static int idio_malloc_findbucket (union idio_malloc_overhead_u *freep, int srchlen);

/*
 * The Emacs/Bash code suggests IDIO_MALLOC_NBUCKETS is SIZEOF_LONG * 4 - 2,
 * usable bins from 1..IDIO_MALLOC_NBUCKETS-1
 */
#define IDIO_MALLOC_NBUCKETS	30

static size_t idio_malloc_bucket_sizes[IDIO_MALLOC_NBUCKETS] = {
    0
};

/*
 * idio_malloc_nextf[i] is the pointer to the next free block of size 2^(i+3).  The
 * smallest allocatable block is 8 bytes.  The idio_malloc_overhead_u information
 * precedes the data area returned to the user.
 */
static union idio_malloc_overhead_u *idio_malloc_nextf[IDIO_MALLOC_NBUCKETS];

static long idio_malloc_pagesz;			/* page size - result from sysconf() */
static int idio_malloc_pagesz_bucket;		/* the page size bucket */

#ifdef IDIO_MALLOC_STATS
/*
 * nmalloc[i] is the difference between the number of mallocs and frees
 * for a given block size.
 */
static u_int nmalloc[IDIO_MALLOC_NBUCKETS];
#include <stdio.h>
#endif

/*
 * idio_malloc_pagealign () is called to grab enough memory to align
 * future allocations on a page boundary and then make use of the
 * (less than a page) allocation it just got.
 *
 * We can avoid any need to invoke any malloc initialisation function
 * because it becomes dynamic.
 */

/* XXX review this choice! */
#define IDIO_MALLOC_PREF_BUCKET	2
#define IDIO_MALLOC_PREF_SIZE	32

static int idio_malloc_pagealign ()
{
    idio_malloc_pagesz = sysconf (_SC_PAGESIZE);
    if (idio_malloc_pagesz < 1024) {
	fprintf (stderr, "im-pagealign: small pagesize() %lx\n", idio_malloc_pagesz);
	idio_malloc_pagesz = 1024;
    }

    char *curbrk = sbrk (0);
    long sbrk_reqd = idio_malloc_pagesz - ((long) curbrk & (idio_malloc_pagesz - 1)); /* sbrk(0) % pagesz */
    if (sbrk_reqd < 0) {
	fprintf (stderr, "sbrk_reqd < 0? curbrk %p pagesz %lx\n", curbrk, idio_malloc_pagesz);
	sbrk_reqd += idio_malloc_pagesz;
    }

    register int nblks;

    if (sbrk_reqd) {
	curbrk = sbrk (sbrk_reqd);
	if ((long)curbrk == -1) {
	    perror ("sbrk");
	    return -1;
	}

	/*
	 * put this allocation in the preferred bin
	 */
	curbrk += sbrk_reqd & (IDIO_MALLOC_PREF_SIZE - 1);
	sbrk_reqd -= sbrk_reqd & (IDIO_MALLOC_PREF_SIZE - 1);
	nblks = sbrk_reqd / IDIO_MALLOC_PREF_SIZE;

	if (nblks > 0) {
	    register union idio_malloc_overhead_u *op = (union idio_malloc_overhead_u *) curbrk;
	    register union idio_malloc_overhead_u *next;

	    idio_malloc_nextf[IDIO_MALLOC_PREF_BUCKET] = op;
	    while (1) {
		op->ov_magic = IDIO_MALLOC_MAGIC_FREE;
		op->ov_bucket = IDIO_MALLOC_PREF_BUCKET;
		nblks--;
		if (nblks <= 0) {
		    break;
		}
		next = (union idio_malloc_overhead_u *) ((char *) op + IDIO_MALLOC_PREF_SIZE);
		IDIO_MALLOC_OVERHEAD_CHAIN (op) = next;
		op = next;
	    }
	    IDIO_MALLOC_OVERHEAD_CHAIN (op) = NULL;
	}
    }

    if (0 == idio_malloc_bucket_sizes[0]) {
	/*
	 * Starting bucket size wants to be sizeof (union
	 * idio_malloc_overhead_u) + 8
	 */
	register unsigned long sz = 8;
	for (nblks = 0; nblks < IDIO_MALLOC_NBUCKETS; nblks++) {
	    idio_malloc_bucket_sizes[nblks] = sz;
	    sz <<= 1;
	}
    }

    for (nblks = 7; nblks < IDIO_MALLOC_NBUCKETS; nblks++) {
	if (idio_malloc_pagesz <= idio_malloc_bucket_sizes[nblks]) {
	    break;
	}
    }
    idio_malloc_pagesz_bucket = nblks;

    return 0;
}

void *idio_malloc_malloc (size_t size)
{
    /*
     * First time malloc is called, setup page size and
     * align break pointer so all data will be page aligned.
     */
    if (idio_malloc_pagesz == 0) {
	if (idio_malloc_pagealign () < 0) {
	    return (void *) NULL;
	}
    }

    /*
     * Convert amount of memory requested into closest block size
     * stored in hash buckets which satisfies request.  Account for
     * space used per block for accounting.
     */
    register long reqd_size = size + sizeof (union idio_malloc_overhead_u) + IDIO_MALLOC_RSLOP;
    register int bucket;
    if (reqd_size <= ((unsigned long) idio_malloc_pagesz >> 1)) {
	bucket = 1;
    } else {
	bucket = idio_malloc_pagesz_bucket;
    }

    for (; bucket < IDIO_MALLOC_NBUCKETS; bucket++) {
	if (reqd_size <= idio_malloc_bucket_sizes[bucket]) {
	    break;
	}
    }

    /* fprintf (stderr, "b: sz %8zu in bkt[%2d] %ld\n", size, bucket, idio_malloc_bucket_sizes[bucket]); */

    /*
     * If nothing in hash bucket right now,
     * request more memory from the system.
     */
    register union idio_malloc_overhead_u *op;
    if ((op = idio_malloc_nextf[bucket]) == NULL) {
	idio_malloc_morecore (bucket);
	if ((op = idio_malloc_nextf[bucket]) == NULL) {
	    fprintf (stderr, "im-malloc: morecore failed\n");
	    return (NULL);
	}
    }
    /* fprintf (stderr, "c: nextf[%2d] = %p\n", bucket, idio_malloc_nextf[bucket]); */
    if ((intptr_t) idio_malloc_nextf[bucket] & 0x3) {
	fprintf (stderr, "op %8p ! pointer\n", op);
	assert (0);
    }

    /* remove from linked list */
    idio_malloc_nextf[bucket] = IDIO_MALLOC_OVERHEAD_CHAIN (op);
    /* fprintf (stderr, "d: nextf[%2d] = %p\n", bucket, idio_malloc_nextf[bucket]); */
    op->ov_magic = IDIO_MALLOC_MAGIC_ALLOC;
    op->ov_bucket = bucket;
#ifdef IDIO_MALLOC_STATS
    nmalloc[bucket]++;
#endif

    /*
     * Record allocated size of block and
     * bound space with magic numbers.
     */
    op->ov_size = size;
    op->ov_rmagic = IDIO_MALLOC_RMAGIC;
    *(uint16_t *)((caddr_t)(op + 1) + op->ov_size) = IDIO_MALLOC_RMAGIC;

    /* fprintf (stderr, "im-malloc: (%5zu)-> %8p\n", size, ((char *)(op + 1))); */
    return ((char *)(op + 1));
}

void * idio_malloc_calloc (size_t num, size_t size)
{
    if (size != 0 && (num * size) / size != num) {
	/* size_t overflow. */
	return (NULL);
    }

    void *ret;

    if ((ret = idio_malloc_malloc(num * size)) != NULL)
	memset(ret, 0, num * size);

    return (ret);
}

/*
 * Allocate more memory to the indicated bucket.
 */
static void idio_malloc_morecore (int bucket)
{
    register long sz = idio_malloc_bucket_sizes[bucket];

    if (sz <= 0) {
	fprintf (stderr, "im-morecore: pagesizes[%d] = %ld\n", bucket, sz);
	return;
    }

    /*
     * We want to allocate a rounded pagesize amount of memory
     */
    int amt;
    int nblks;
    if (sz < idio_malloc_pagesz) {
	amt = idio_malloc_pagesz;
	nblks = amt / sz;
    } else {
	/*
	 * The Bash algorithm checks alignment
	 */
	amt = sz & (idio_malloc_pagesz - 1);
	if (0 == amt) {
	    amt = sz;
	} else {
	    amt = sz + idio_malloc_pagesz - amt;
	}
	nblks = 1;
    }

    /* fprintf (stderr, "im-morecore: pagesizes[%2d] sz %8ld amt %8d in %4d blks\n", bucket, sz, amt, nblks); */
    register union idio_malloc_overhead_u *op;
    op = (union idio_malloc_overhead_u *) mmap (0, amt, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    /* fprintf (stderr, "im-morecore: pagesizes[%2d] op %8p CHAIN %8p\n", bucket, op, IDIO_MALLOC_OVERHEAD_CHAIN (op)); */
    if ((void *) op != (void *) -1) {
	idio_malloc_nextf[bucket] = op;

	while (1) {
	    op->ov_magic = IDIO_MALLOC_MAGIC_FREE;
	    op->ov_bucket = bucket;
	    nblks--;
	    if (nblks <= 0) {
		break;
	    }
	    register union idio_malloc_overhead_u *next = (union idio_malloc_overhead_u *) ((char *) op + sz);
	    IDIO_MALLOC_OVERHEAD_CHAIN (op) = next;
	    op = next;
	}
	IDIO_MALLOC_OVERHEAD_CHAIN (op) = NULL;
    }
    /* fprintf (stderr, "im-morecore: pagesizes[%2d] op %8p CHAIN %8p\n", bucket, idio_malloc_nextf[bucket], IDIO_MALLOC_OVERHEAD_CHAIN (idio_malloc_nextf[bucket])); */
}

void idio_malloc_free (void *cp)
{
    if (cp == NULL) {
	return;
    }

    register union idio_malloc_overhead_u *op;
    op = (union idio_malloc_overhead_u *)((caddr_t)cp - sizeof (union idio_malloc_overhead_u));

    if (op->ov_magic != IDIO_MALLOC_MAGIC_ALLOC) {
	fprintf (stderr, "im-free: !ALLOC: cp %8p op %8p: magic %#x index %d\n", cp, op, op->ov_magic, op->ov_bucket);
	if (op->ov_magic == IDIO_MALLOC_MAGIC_FREE) {
	    fprintf (stderr, "im-free: already freed??\n");
	} else {
	    fprintf (stderr, "im-free: magic is %#x??\n", op->ov_magic);
	}
	assert (0);
    }

    IDIO_C_ASSERT(op->ov_rmagic == IDIO_MALLOC_RMAGIC);
    /* IDIO_C_ASSERT(*(uint16_t *)((caddr_t)(op + 1) + op->ov_size) == IDIO_MALLOC_RMAGIC); */

    register int bucket = op->ov_bucket;

    IDIO_C_ASSERT (bucket < IDIO_MALLOC_NBUCKETS);
    
    /*
     * If someone ran cp[-1] = RMAGIC then we don't know what else
     * they've done.  Not that there's *that* much we can do as any of
     * the values could have been trampled on.
     */
    register long reqd_size = op->ov_size + sizeof (union idio_malloc_overhead_u) + IDIO_MALLOC_RSLOP;
    if (reqd_size > idio_malloc_bucket_sizes[bucket]) {
	fprintf (stderr, "im-realloc: %ld (%d)> bucket[%2d] == %zu\n", reqd_size, op->ov_size, bucket, idio_malloc_bucket_sizes[bucket]);
	IDIO_C_ASSERT (0);
    }

#if IDIO_DEBUG
    /*
     * memset to something not all-zeroes and not all-ones to try to
     * catch assumptions about default memory bugs
     *
     * Also be different to idio_gc_alloc() which uses 0x37
     */
    memset (cp, 0x38, op->ov_size);
#endif
    
    op->ov_magic = IDIO_MALLOC_MAGIC_FREE;
    IDIO_MALLOC_OVERHEAD_CHAIN (op) = idio_malloc_nextf[bucket];	/* also clobbers ov_magic */
    idio_malloc_nextf[bucket] = op;
#ifdef IDIO_MALLOC_STATS
    nmalloc[bucket]--;
#endif

    /* fprintf (stderr, "im-free: cp %8p -> nextf[%2d] %8p CHAIN %8p\n", cp, bucket, op, IDIO_MALLOC_OVERHEAD_CHAIN (op)); */
}

/*
 * The Bash algorithm seems more succint
 */

void * idio_malloc_realloc (void *cp, size_t size)
{
    /* fprintf (stderr, "im-realloc: %8p + %8zu\n", cp, size); */

    if (0 == size) {
	idio_malloc_free (cp);
	return NULL;
    }

    if (NULL == cp) {
	return idio_malloc_malloc (size);
    }

    union idio_malloc_overhead_u *op;
    op = (union idio_malloc_overhead_u *) ((caddr_t) cp - sizeof (union idio_malloc_overhead_u));

    if (op->ov_magic != IDIO_MALLOC_MAGIC_ALLOC) {
	fprintf (stderr, "im-realloc: !ALLOC: cp %8p op %8p: magic %#x index %d\n", cp, op, op->ov_magic, op->ov_bucket);
	if (op->ov_magic == IDIO_MALLOC_MAGIC_FREE) {
	    fprintf (stderr, "im-realloc: already freed??\n");
	} else {
	    fprintf (stderr, "im-realloc: magic is %#x??\n", op->ov_magic);
	}
	assert (0);
    }

    IDIO_C_ASSERT (op->ov_rmagic == IDIO_MALLOC_RMAGIC);
    /* IDIO_C_ASSERT(*(uint16_t *)((caddr_t)(op + 1) + op->ov_size) == IDIO_MALLOC_RMAGIC); */

    register int bucket = op->ov_bucket;

    IDIO_C_ASSERT (bucket < IDIO_MALLOC_NBUCKETS);
    
    /*
     * If someone ran cp[-1] = RMAGIC then we don't know what else
     * they've done.  Not that there's *that* much we can do as any of
     * the values could have been trampled on.
     */
    register long reqd_size = op->ov_size + sizeof (union idio_malloc_overhead_u) + IDIO_MALLOC_RSLOP;
    if (reqd_size > idio_malloc_bucket_sizes[bucket]) {
	fprintf (stderr, "im-realloc: %ld (%d) > bucket[%2d] == %zu\n", reqd_size, op->ov_size, bucket, idio_malloc_bucket_sizes[bucket]);
	IDIO_C_ASSERT (0);
    }

    if (size == op->ov_size) {
	return cp;
    }

    if (0 == bucket) {
	fprintf (stderr, "BUCKET_RANGE (%ld, %d)?\n", reqd_size, bucket);
    }
    /*
     * Rework with the (actual) requested size -- do we fit in this
     * bucket anyway and can we get away with just rejigging the
     * allocation's ov_size?
     */
    reqd_size = size + sizeof (union idio_malloc_overhead_u) + IDIO_MALLOC_RSLOP;
    if (IDIO_MALLOC_BUCKET_RANGE (reqd_size, bucket) ||
	IDIO_MALLOC_BUCKET_RANGE (reqd_size, bucket - 1)) {
	/* fprintf (stderr, "BUCKET_RANGE %d -> %zu\n", op->ov_size, size); */
	op->ov_size = size;
	return cp;
    }

    register uint32_t count = op->ov_size;
    if (reqd_size < count) {
	count = reqd_size;
    }

    register char *res;
    if ((res = idio_malloc_malloc (size)) == NULL) {
	return NULL;
    }
    
    bcopy(cp, res, count);

    idio_malloc_free (cp);
    
    return res;
}

/*
 * Search ``srchlen'' elements of each free list for a block whose
 * header starts at ``freep''.  If srchlen is -1 search the whole list.
 * Return bucket number, or -1 if not found.
 */
static int idio_malloc_findbucket (union idio_malloc_overhead_u *freep, int srchlen)
{
    register union idio_malloc_overhead_u *p;
    register int i, j;

    for (i = 0; i < IDIO_MALLOC_NBUCKETS; i++) {
	j = 0;
	for (p = idio_malloc_nextf[i]; p && j != srchlen; p = IDIO_MALLOC_OVERHEAD_CHAIN (p)) {
	    if (p == freep)
		return (i);
	    j++;
	}
    }
    return (-1);
}

#ifdef IDIO_MALLOC_STATS
/*
 * mstats - print out statistics about malloc
 *
 * Prints two lines of numbers, one showing the length of the free list
 * for each size category, the second showing the number of mallocs -
 * frees for each size category.
 */
idio_malloc_mstats (char *s)
{
    register int i, j;
    register union idio_malloc_overhead_u *p;
    int totfree = 0,
  	totused = 0;

    fprintf(stderr, "Memory allocation statistics %s\nfree:\t", s);
    for (i = 0; i < IDIO_MALLOC_NBUCKETS; i++) {
	for (j = 0, p = idio_malloc_nextf[i]; p; p = IDIO_MALLOC_OVERHEAD_CHAIN (p), j++)
	    ;
	fprintf(stderr, " %d", j);
	totfree += j * (1 << (i + 3));
    }
    fprintf(stderr, "\nused:\t");
    for (i = 0; i < IDIO_MALLOC_NBUCKETS; i++) {
	fprintf(stderr, " %d", nmalloc[i]);
	totused += nmalloc[i] * (1 << (i + 3));
    }
    fprintf(stderr, "\n\tTotal in use: %d, total free: %d\n",
	    totused, totfree);
}
#endif

/*
 * http://stackoverflow.com/questions/3774417/sprintf-with-automatic-memory-allocation
 */
int idio_malloc_vasprintf (char **strp, const char *fmt, va_list ap)
{
    va_list ap1;
    size_t size;
    char *buffer;

    va_copy (ap1, ap);
    size = vsnprintf (NULL, 0, fmt, ap1) + 1;
    va_end (ap1);
    buffer = idio_malloc_calloc (1, size);

    if (!buffer)
        return -1;

    *strp = buffer;

    return vsnprintf (buffer, size, fmt, ap);
}

int idio_malloc_asprintf(char **strp, const char *fmt, ...)
{
    int error;
    va_list ap;

    va_start (ap, fmt);
    error = idio_malloc_vasprintf (strp, fmt, ap);
    va_end (ap);

    return error;
}
