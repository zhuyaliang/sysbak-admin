

#include "libxfs_priv.h"

/*
 * Simple memory interface
 */

kmem_zone_t *
kmem_zone_init(int size, char *name)
{
	kmem_zone_t	*ptr = malloc(sizeof(kmem_zone_t));

	if (ptr == NULL) {
		exit(1);
	}
	ptr->zone_unitsize = size;
	ptr->zone_name = name;
	ptr->allocated = 0;
	return ptr;
}

void *
kmem_zone_alloc(kmem_zone_t *zone, int flags)
{
	void	*ptr = malloc(zone->zone_unitsize);

	if (ptr == NULL) {
		exit(1);
	}
	zone->allocated++;
	return ptr;
}
void *
kmem_zone_zalloc(kmem_zone_t *zone, int flags)
{
	void	*ptr = kmem_zone_alloc(zone, flags);

	memset(ptr, 0, zone->zone_unitsize);
	return ptr;
}


void *
kmem_alloc(size_t size, int flags)
{
	void	*ptr = malloc(size);

	if (ptr == NULL) {
		exit(1);
	}
	return ptr;
}

void *
kmem_zalloc(size_t size, int flags)
{
	void	*ptr = kmem_alloc(size, flags);

	memset(ptr, 0, size);
	return ptr;
}

void *
kmem_realloc(void *ptr, size_t new_size, int flags)
{
	ptr = realloc(ptr, new_size);
	if (ptr == NULL) {
		exit(1);
	}
	return ptr;
}
