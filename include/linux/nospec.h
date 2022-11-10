// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Linus Torvalds. All rights reserved.
// Copyright(c) 2018 Alexei Starovoitov. All rights reserved.
// Copyright(c) 2018 Intel Corporation. All rights reserved.

#ifndef _LINUX_NOSPEC_H
#define _LINUX_NOSPEC_H

#include <linux/compiler.h>
#include <asm/barrier.h>

struct task_struct;

/**
 * array_index_mask_nospec() - generate a ~0 mask when index < size, 0 otherwise
 * @index: array element index
 * @size: number of elements in array
 *
 * When @index is out of bounds (@index >= @size), the sign bit will be
 * set.  Extend the sign bit to all bits and invert, giving a result of
 * zero for an out of bounds index, or ~0 if within bounds [0, @size).
 */
#ifndef array_index_mask_nospec
static inline unsigned long array_index_mask_nospec(unsigned long index,
						    unsigned long size)
{
	/*
	 * Always calculate and emit the mask even if the compiler
	 * thinks the mask is not needed. The compiler does not take
	 * into account the value of @index under speculation.
	 */
	OPTIMIZER_HIDE_VAR(index);
	return ~(long)(index | (size - 1UL - index)) >> (BITS_PER_LONG - 1);
}
#endif

/*
 * array_index_nospec - sanitize an array index after a bounds check
 *
 * For a code sequence like:
 *
 *     if (index < size) {
 *         index = array_index_nospec(index, size);
 *         val = array[index];
 *     }
 *
 * ...if the CPU speculates past the bounds check then
 * array_index_nospec() will clamp the index within the range of [0,
 * size).
 */
#define array_index_nospec(index, size)					\
({									\
	typeof(index) _i = (index);					\
	typeof(size) _s = (size);					\
	unsigned long _mask = array_index_mask_nospec(_i, _s);		\
									\
	BUILD_BUG_ON(sizeof(_i) > sizeof(long));			\
	BUILD_BUG_ON(sizeof(_s) > sizeof(long));			\
									\
	(typeof(_i)) (_i & _mask);					\
})

#ifndef barrier_nospec
#define barrier_nospec()	do { } while (0)
#endif

/*
 * neq_mask_nospec() - generate a 0 mask when x != y, otherwise x
 * @x: First value
 * @y: Second value
 */
static inline unsigned long neq_mask_nospec(unsigned long x, unsigned long y)
{
	/*
	 * Always calculate and emit the mask even if the compiler
	 * thinks the mask is not needed. The compiler does not take
	 * into account the value of @x and @y under speculation.
	 */
	OPTIMIZER_HIDE_VAR(x);
	OPTIMIZER_HIDE_VAR(y);
	return ((long)((x ^ y) ^ ((x ^ y) - 1))) >> (BITS_PER_LONG - 1);
}

/*
 * magic_neq_nospec - sanitize a struct pointer by comparing p->spec_magic
 *		      with a build time constant. If equal, p is returned,
 *		      otherwise 0.
 *
 * For a code sequence like:
 *
 *         p = magic_neq_nospec(p, MAGIC);
 *         x = p->val;
 *
 * ...if the CPU speculates on a wrong value of p, magic_neq_nospec() will
 *    zero-out p and subsequent accesses using p can't be controlled.
 *
 *    User of this macro has to ensure that p->spec_magic exists and is
 *    initialized to MAGIC(ideally in some init code) before magic_neq_nospec()
 *    is invoked.
 */
#define magic_neq_nospec(p, magic)						\
({										\
	typeof(p) _p = (p);							\
	typeof(_p->spec_magic) _p_spec_magic = (_p->spec_magic);		\
	typeof(magic) _magic = (magic);						\
	unsigned long _mask = neq_mask_nospec(_p_spec_magic, _magic);		\
										\
	BUILD_BUG_ON(sizeof(_p_spec_magic) > sizeof(long));			\
	BUILD_BUG_ON(sizeof(_magic) > sizeof(long));				\
	BUILD_BUG_ON(sizeof(_p) > sizeof(long));				\
										\
	(typeof(_p))((unsigned long)_p & _mask);				\
})

/* Speculation control prctl */
int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which);
int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			     unsigned long ctrl);
/* Speculation control for seccomp enforced mitigation */
void arch_seccomp_spec_mitigate(struct task_struct *task);

#endif /* _LINUX_NOSPEC_H */
