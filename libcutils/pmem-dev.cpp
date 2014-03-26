#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/android_pmem.h>
#include <cutils/pmem.h>
#include <utils/Log.h>


#undef  LOG_TAG
#define LOG_TAG "utils_pmem"

#include <unwind.h>
#include <stdlib.h>
#include <cutils/properties.h>

#define PMEM_DEVICE_NAME "/dev/pmem_multimedia"
#define IOCTL_FAILED      -1
#define UNMAP_FAILED      -1
#define CLOSE_FAILED      -1
#define INVALID_FD        -1


static char p_value[PROPERTY_VALUE_MAX] = "";
static bool p_res = 0;

    
static size_t align_size(size_t size)
{
	return ((size + PAGE_SIZE-1) & ~(PAGE_SIZE-1));
}

// =============================================================================
// stack trace functions
// =============================================================================
//
// The statck related function is copy from bionic
// 

typedef struct
{
    size_t count;
    intptr_t* addrs;
} stack_crawl_state_t;

/* depends how the system includes define this */
#ifdef HAVE_UNWIND_CONTEXT_STRUCT
typedef struct _Unwind_Context __unwind_context;
#else
typedef _Unwind_Context __unwind_context;
#endif

static _Unwind_Reason_Code trace_function(__unwind_context *context, void *arg)
{
    stack_crawl_state_t* state = (stack_crawl_state_t*)arg;
    if (state->count) {
        intptr_t ip = (intptr_t)_Unwind_GetIP(context);
        if (ip) {
            state->addrs[0] = ip;
            state->addrs++;
            state->count--;
            return _URC_NO_REASON;
        }
    }
    /*
     * If we run out of space to record the address or 0 has been seen, stop
     * unwinding the stack.
     */
    return _URC_END_OF_STACK;
}

static inline
int get_backtrace(intptr_t* addrs, size_t max_entries)
{
    stack_crawl_state_t state;
    state.count = max_entries;
    state.addrs = (intptr_t*)addrs;
    _Unwind_Backtrace(trace_function, (void*)&state);
    return max_entries - state.count;
}

static void dump_stack_trace()
{
    intptr_t addrs[20];
    int c = get_backtrace(addrs, 20);
    char buf[21];
    char tmp[21*20];
    int i;

    tmp[0] = 0; // Need to initialize tmp[0] for the first strcat
    for (i=0 ; i<c; i++) {
        snprintf(buf, sizeof buf, "[PMEM]%2d: %08x\n", i, addrs[i]);
        strlcat(tmp, buf, sizeof tmp);
    }
    //__libc_android_log_print(ANDROID_LOG_ERROR, "libc", "call stack:\n%s", tmp);
    ALOGE("[PMEM] call stack:\n%s", tmp);
}

void* pmem_alloc(size_t size, int *pfd)
{
    int         fd;
    size_t      aligned_size;
    void*       base;
    pmem_region region = { 0, 0 };
    int         err;

    if (NULL == pfd)
    {
        return NULL;
    }

    *pfd = -1;
    
    fd = open(PMEM_DEVICE_NAME, (O_RDWR | O_SYNC));
    if (INVALID_FD == fd)
    {
        ALOGE("[PMEM] open %s failed!", PMEM_DEVICE_NAME);
        goto open_failed;
    }

    aligned_size = align_size(size);
    base = mmap(0, aligned_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (MAP_FAILED == base) 
    {
        ALOGE("[PMEM] mmap size %d failed!", aligned_size);
        goto mmap_failed;
    }
    
    region.len = aligned_size;
    err = ioctl(fd, PMEM_MAP, &region);
    if (IOCTL_FAILED == err)
    {
        ALOGE("[PMEM] PMEM_MAP size %d failed!", aligned_size);
        goto pmem_map_failed;
    }

    property_get("pm.dumpstack", p_value, "0"); //if not set, disable by default 
    p_res = atoi(p_value);
    if (p_res) 
    {
        ALOGE("[PMEM] pmem_alloc: base: 0x%08x, size: %d\n", (int)base, aligned_size);
        dump_stack_trace();
    }

    *pfd = fd;
    
    return base;


insert_failed:
    ioctl(fd, PMEM_UNMAP, &region);
pmem_map_failed:
    munmap(base, aligned_size);
mmap_failed:
    close(fd);
open_failed:
    return NULL;    
}


void* pmem_alloc_sync(size_t size, int *pfd)
{
    int         fd;
    size_t      aligned_size;
    void*       base;
    pmem_region region = { 0, 0 };
    int         err;

	if (NULL == pfd)
	{
		return NULL;
	}

	*pfd = -1;
    
    fd = open(PMEM_DEVICE_NAME, O_RDWR|O_SYNC);
    if (INVALID_FD == fd)
    {
        ALOGE("[PMEM] open %s failed!", PMEM_DEVICE_NAME);
        goto open_failed;
    }

    aligned_size = align_size(size);
    base = mmap(0, aligned_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (MAP_FAILED == base) 
    {
        ALOGE("[PMEM] mmap size %d failed!", aligned_size);
        goto mmap_failed;
    }
    
    region.len = aligned_size;
    err = ioctl(fd, PMEM_MAP, &region);
    if (IOCTL_FAILED == err)
    {
        ALOGE("[PMEM] PMEM_MAP size %d failed!", aligned_size);
        goto pmem_map_failed;
    }

    property_get("pm.dumpstack", p_value, "0"); //if not set, disable by default 
    p_res = atoi(p_value);
    if (p_res) 
    {
        ALOGE("[PMEM] pmem_alloc_sync: base: 0x%08x, size: %d\n", (int)base, aligned_size);
        dump_stack_trace();
    }

    *pfd = fd;
    return base;


insert_failed:
    ioctl(fd, PMEM_UNMAP, &region);
pmem_map_failed:
    munmap(base, aligned_size);
mmap_failed:
    close(fd);
open_failed:
    return NULL;    
}


int  pmem_free(void *ptr, size_t size, int fd)
{
    int err, ret = 0;
    size_t aligned_size = align_size(size);    

    pmem_region region = { 0, aligned_size };
    err = ioctl(fd, PMEM_UNMAP, &region);
    if (IOCTL_FAILED == err)
    {
        ALOGE("PMEM_UNMAP size %d failed!", size);
        ret = err;
    }
    
    err = munmap(ptr, aligned_size);
    if (UNMAP_FAILED == err)
    {
        ALOGE("mumap size %d failed!", size);
        ret = err;
    }    

    err = close(fd);
    if (CLOSE_FAILED == err)
    {
        ALOGE("Close file %d failed!", fd);
        ret = err;
    }


    property_get("pm.dumpstack", p_value, "0"); //if not set, disable by default 
    p_res = atoi(p_value);
    if (p_res) 
    {
        ALOGE("[PMEM] pmem_free: base: 0x%08x, size: %d\n", (int)ptr, aligned_size);
        dump_stack_trace();
    }

    
    return ret;
}


void* pmem_get_phys(int fd)
{
    pmem_region pmem_reg;
    int err = ioctl(fd, PMEM_GET_PHYS, &pmem_reg);
    if (IOCTL_FAILED == err)
    {
        ALOGE("PMEM_GET_PHYS failed: 0x%X !", fd);
        return NULL;
    }

    return (void*)(pmem_reg.offset);
}

void pmem_cache_flush(int fd, unsigned int offset, unsigned int length) {
    pmem_region region = { 0, 0 };
    int         err = -1; 

    if (fd < 0 || offset == 0 || length == 0) {
        ALOGE("pmem_cache_flush: invalide argument\n");
        return;
    }   

    region.offset = offset;
    region.len = length;
    err = ioctl(fd, PMEM_CACHE_FLUSH, &region);
    if (IOCTL_FAILED == err)
    {   
        ALOGE("PMEM_CACHE_FLUSH offset 0x%08x, size %d failed!\n", offset, length);
    }
}

