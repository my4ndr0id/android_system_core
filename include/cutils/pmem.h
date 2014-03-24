
#ifndef _CUTILS_PMEM_H
#define _CUTILS_PMEM_H

#ifdef __cplusplus
extern "C" {
#endif

void* pmem_alloc(size_t size, int *pfd);

void* pmem_alloc_sync(size_t size, int *pfd);

int  pmem_free(void *ptr, size_t size, int fd);

void* pmem_get_phys(int fd);

#ifdef __cplusplus
}
#endif

#endif

