#include "memory.h"
#include "memory_internal.h"

static inline uint32_t *get_entry(const uint32_t *table, const size_t entry)
{
	return (uint32_t *) (table[entry] & PAGING_ADDR_MASK);
}

bool paging_is_allocated(const uint32_t *directory,
	const void *ptr, const size_t length)
{
	// TODO Optimize like paging_find_free
	const size_t page = ptr_to_page(ptr);

	for(size_t i = 0; i < length; ++i)
		if(!(*paging_get_page(directory, page + i) & PAGING_PAGE_PRESENT))
			return false;

	return true;
}

static bool pages_fit(uint32_t *directory,
	const size_t begin, const size_t length)
{
	const size_t t = begin / PAGING_DIRECTORY_SIZE;
	const size_t p = begin % PAGING_DIRECTORY_SIZE;

	size_t n = 0;

	for(size_t i = t; i < PAGING_DIRECTORY_SIZE; ++i)
	{
		const uint32_t *table = get_entry(directory, i);

		if(*table & PAGING_TABLE_PRESENT)
			for(size_t j = (i == t ? p : 0); j < PAGING_TABLE_SIZE; ++j)
			{
				if(*get_entry(table, j) & PAGING_PAGE_PRESENT)
					n = 0;
				else
					++n;

			}
		else
			n += PAGING_TABLE_SIZE;

		if(n >= length)
			return true;
	}

	return false;
}

static int paging_find_free(uint32_t *directory, const size_t length)
{
	// TODO Avoid iterating over every page (make pages_fit return the next hole)

	for(size_t i = 0; i < PAGING_TOTAL_PAGES; ++i)
		if(pages_fit(directory, i, length))
			return i;

	return -1;
}

void *paging_alloc(uint32_t *directory, void *hint,
	const size_t length, const paging_flags_t flags)
{
	// TODO Bulk physical allocation
	if(!directory || length == 0) return NULL;

	if(hint && !paging_is_allocated(directory, hint, length))
	{
		const size_t page = ptr_to_page(hint);

		for(size_t i = page; i < page + length; ++i)
			paging_set_page(directory, i, physical_alloc(), flags);

		return hint;
	}

	const int i = paging_find_free(directory, length);
	if(i < 0) return NULL;

	for(size_t j = i; j < i + length; ++j)
		paging_set_page(directory, j, physical_alloc(), flags);

	return page_to_ptr(i);
}

void paging_free(uint32_t *directory, void *ptr, const size_t length)
{
	if(!directory || !ptr || length == 0) return;

	const size_t page = ptr_to_page(ptr);

	for(size_t i = 0; i < length; ++i)
		paging_set_page(directory, page + i, 0, 0);
}

uint32_t *paging_get_page(const uint32_t *directory, const size_t page)
{
	if(!directory) return NULL;

	const size_t t = page / PAGING_DIRECTORY_SIZE;
	const size_t p = page % PAGING_TABLE_SIZE;

	if(!(directory[t] & PAGING_TABLE_PRESENT)) return NULL;
	return get_entry(directory, t) + p;
}

static bool is_table_empty(uint32_t *directory, const size_t i)
{
	if(!directory) return false;

	const uint32_t *table = get_entry(directory, i);
	if(!table) return false;

	for(size_t j = 0; j < PAGING_TABLE_SIZE; ++j)
		if(table[j] & PAGING_PAGE_PRESENT)
			return false;

	return true;
}

void paging_set_page(uint32_t *directory, const size_t page,
	void *physaddr, const paging_flags_t flags)
{
	if(!directory) return;

	const size_t t = page / PAGING_DIRECTORY_SIZE;
	const size_t p = page % PAGING_TABLE_SIZE;

	if(!(directory[t] & PAGING_TABLE_PRESENT))
	{
		if(!(flags & PAGING_PAGE_PRESENT)) return;

		void *ptr;
		if(!(ptr = physical_alloc())) return;
		bzero(ptr, PAGE_SIZE);

		directory[t] = (uintptr_t) ptr
			| PAGING_TABLE_PRESENT | (flags & 0b111111);
	}

	get_entry(directory, t)[p] |= ((uintptr_t) physaddr & PAGING_ADDR_MASK)
		| (flags & PAGING_FLAGS_MASK);

	if(!(flags & PAGING_PAGE_PRESENT) && is_table_empty(directory, t))
	{
		physical_free(get_entry(directory, t));
		directory[t] = 0;
	}
}