
#include <btrfs/kerncompat.h>
#include <btrfs/radix-tree.h>
#ifdef __KERNEL__
#define RADIX_TREE_MAP_SHIFT	(CONFIG_BASE_SMALL ? 4 : 6)
#else
#define RADIX_TREE_MAP_SHIFT	3	/* For more stressful testing */
#endif

#define RADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH (RADIX_TREE_INDEX_BITS/RADIX_TREE_MAP_SHIFT + 2)

static unsigned long height_to_maxindex[RADIX_TREE_MAX_PATH] __read_mostly;

static unsigned long __maxindex(unsigned int height)
{
	unsigned int tmp = height * RADIX_TREE_MAP_SHIFT;
	unsigned long index = ~0UL;

	if (tmp < RADIX_TREE_INDEX_BITS)
		index = (index >> (RADIX_TREE_INDEX_BITS - tmp - 1)) >> 1;
	return index;
}

static void radix_tree_init_maxindex(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(height_to_maxindex); i++)
		height_to_maxindex[i] = __maxindex(i);
}

void radix_tree_init(void)
{
	radix_tree_init_maxindex();
}
