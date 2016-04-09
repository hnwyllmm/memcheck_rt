#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#include "memcheck_rt.h"
#include "memcheck_hashtable.h"

#ifdef DEBUG_MEMCHECK_RT
#define RT_TRACE	printf
#else
#define RT_TRACE(...)
#endif // DEBUG_MEMCHECK_RT

#define RT_ENV_SIGNO	"RT_ENV_SIGNO"
#define RT_ENV_FILE		"RT_ENV_FILE"
#define RT_DEFAULT_FILE	"memcheck_rt.log"

///////////////////////////////////////////////////////////////////////////////
// predefines

static void rt_init_hook(void) ;
static void * rt_malloc_hook(size_t size, const void *caller) ;
static void * rt_realloc_hook(void *ptr, size_t size, const void *caller) ;
static void rt_free_hook(void *ptr, const void *caller) ;

static void *(*old_malloc_hook)(size_t, const void *)  ;
static void *(*old_realloc_hook)(size_t, const void *)  ;
static void (*old_free_hook) (void *, const void *) ;
static void *(*bak_malloc_hook)(size_t, const void *)  ;
static void *(*bak_realloc_hook)(size_t, const void *)  ;
static void (*bak_free_hook) (void *, const void *) ;

static void sig_record(int signo) ;

static FILE *record_fp = NULL ;
static pthread_mutex_t __lock = PTHREAD_MUTEX_INITIALIZER ;
static pthread_mutex_t lock_record = PTHREAD_MUTEX_INITIALIZER ;

/// there are some stack in this libary which is no use for user
/// please modify the value if necessary
static const int useful_stack_start = 2 ;

///////////////////////////////////////////////////////////////////////////////
// some predefined function

///////////////////////////////////////////////////////////////////////////////
// thread mutex
#if 0
int __attribute__((weak))
	pthead_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) 
{
	return 0;
}

int __attribute__((weak)) pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	return 0;
}
int __attribute__((weak)) pthread_mutex_lock(pthread_mutex_t *mutex)
{
	return 0;
}

int __attribute__((weak)) pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	return 0;
}
#endif

///////////////////////////////////////////////////////////////////////////////
// real function for `malloc', `realloc' and `free'

static __attribute__((always_inline)) void nohook_begin()
{
	pthread_mutex_lock(&__lock) ;
	bak_malloc_hook 	= __malloc_hook ;
	bak_realloc_hook 	= __realloc_hook ;
	bak_free_hook		= __free_hook ;
	
	__malloc_hook		= old_malloc_hook ;
	__realloc_hook		= old_realloc_hook ;
	__free_hook			= old_free_hook ;
}

static __attribute__((always_inline)) void nohook_end()
{
	old_malloc_hook	= __malloc_hook ;
	old_realloc_hook= __realloc_hook ;
	old_free_hook	= __free_hook ;

	__malloc_hook 	= bak_malloc_hook ;
	__realloc_hook	= bak_realloc_hook ;
	__free_hook 	= bak_free_hook ;
	pthread_mutex_unlock(&__lock ) ;
}

static inline void *real_malloc(size_t size)
{
	void *result ;

	pthread_mutex_lock(&__lock) ;
	bak_malloc_hook = __malloc_hook ;
	__malloc_hook = old_malloc_hook ;
	result = malloc(size) ;
	old_malloc_hook = __malloc_hook ;
	__malloc_hook = bak_malloc_hook ;
	pthread_mutex_unlock(&__lock) ;

	return result ;
}

static inline void *real_realloc(void *ptr, size_t size)
{
	void *result ;

	pthread_mutex_lock(&__lock) ;
	bak_realloc_hook = __realloc_hook ;
	__realloc_hook = old_realloc_hook ;
	result = realloc(ptr, size) ;
	old_realloc_hook = __realloc_hook ;
	__realloc_hook = bak_realloc_hook ;
	pthread_mutex_unlock(&__lock) ;

	return result ;
}

static inline void real_free(void *ptr)
{
	pthread_mutex_lock(&__lock) ;
	bak_free_hook = __free_hook ;
	__free_hook = old_free_hook ;
	free(ptr) ;
	old_free_hook = __free_hook ;
	__free_hook = bak_free_hook ;
	pthread_mutex_unlock(&__lock) ;
}

///////////////////////////////////////////////////////////////////////////////
// records
// there are two hash tables, one for malloc information with stack and symbols
// and the other for recording pointer and size corresponding
static DECLARE_HASHTABLE(malloc_info_hash_table, 10) ;
static DECLARE_HASHTABLE(pointer_info_hash_table, 10) ;

#ifdef __cpluscplus
extern "C"	{
#endif
static void memcheck_dump(FILE *fp) ;
static void memcheck_dump_safe(FILE *fp) ;
static void memcheck_dump_console();
#ifdef __cpluscplus
}
#endif 

static void __attribute__((constructor)) memcheck_record_init(void)
{
	// pthread_mutex_init(&lock_record, NULL) ;
}

static void __attribute__((destructor)) memcheck_record_exit(void)
{
	memcheck_dump_safe(record_fp) ;
}

struct memcheck_hnode
{
	struct hlist_node		hnode ;
	struct malloc_record	record ;
};

struct pointer_hnode
{
	struct hlist_node		hnode ;
	size_t					size ;
	void *					pointer ;
	struct malloc_record *	record ;
};

static inline void *malloc_info_key(void *stack_info[], int stack_size)
{
	return stack_info[stack_size / 2] ;
}

static struct memcheck_hnode *memcheck_hnode_find(void *stack_info[], int stack_size)
{
	void *key = malloc_info_key(stack_info, stack_size) ;
	const int index = hash_min(key, HASH_BITS(malloc_info_hash_table)) ;
	struct hlist_head *hhead = &malloc_info_hash_table[index] ;
	struct memcheck_hnode *pos ;

	hlist_for_each_entry(pos, hhead, hnode)
	{
		if (pos->record.stack_size == stack_size &&
			0 == memcmp(pos->record.stack_info, stack_info, 
					stack_size * sizeof(void *) )
		   )
		{
			printf("%p %p\n", pthread_self(), pos->record.stack_info) ;
			return pos ;
		}
	}
	return NULL ;
}

static struct pointer_hnode *pointer_hnode_find(void *ptr)
{
	const int index = hash_min(ptr, HASH_BITS(pointer_info_hash_table)) ;
	struct hlist_head *hhead = &pointer_info_hash_table[index] ;
	struct pointer_hnode *pos ;
	hlist_for_each_entry(pos, hhead, hnode)
	{
		if (pos->pointer == ptr)
			return pos ;
	}
	return NULL ;
}

/// package some routines with malloc and free
/// use `always_inline' can reduce the stack level
static __attribute__((always_inline)) int nohook_backtrace(void **buffer, int size)
{
	nohook_begin() ;
	size = backtrace(buffer, size) ;
	nohook_end() ;
	return size ;
}

static __attribute__((always_inline)) char **nohook_backtrace_symbols(void *const *buffer, int size)
{
	char **ret ;
	nohook_begin() ;
	ret = backtrace_symbols(buffer, size) ;
	nohook_end() ;
	return ret ;
}

/// record `malloc', `realloc' and `free'
static void memcheck_record_malloc(void *ptr, size_t size)
{
	struct memcheck_hnode *mc_hnode ;
	struct pointer_hnode *ptr_hnode ;
	void *stack_info[RT_MAX_STACK_SIZE] ;
	int stack_size ;

	stack_size = nohook_backtrace(stack_info, RT_STACK_SIZE) ;
	if (stack_size <= 0)
		return ;

	mc_hnode = memcheck_hnode_find(stack_info, stack_size) ;
	if (!mc_hnode )
	{
		mc_hnode = (struct memcheck_hnode *)
			real_malloc(sizeof(struct memcheck_hnode) ) ;

#ifdef DEBUG_MEMCHECK_RT
		{
			const int index = hash_min(malloc_info_key(stack_info, stack_size),
					HASH_BITS(malloc_info_hash_table)) ;
			RT_TRACE("new malloc info hash node: %d\n", index) ;
		}
#endif
		mc_hnode->record.record_type = MT_MALLOC ;
		mc_hnode->record.stack_size  = stack_size ;
		mc_hnode->record.mem_size    = 0 ;
		mc_hnode->record.stack_info  = real_malloc(sizeof(void *) * stack_size) ;
		if (!mc_hnode->record.stack_info)
		{
			real_free(mc_hnode) ;
			return ;
		}
		memcpy(mc_hnode->record.stack_info, stack_info, sizeof(void *) * stack_size) ;
		mc_hnode->record.symbols = NULL ;// nohook_backtrace_symbols(stack_info, stack_size) ;

		hash_add(malloc_info_hash_table, &mc_hnode->hnode, 
				malloc_info_key(stack_info, stack_size)) ;
	}
	if (!mc_hnode)
		return ;

	mc_hnode->record.mem_size += size ;

	ptr_hnode = pointer_hnode_find(ptr) ;
	if (ptr_hnode)
	{
		RT_TRACE("pointer already exist! %p\n", ptr) ;
	}
	ptr_hnode = (struct pointer_hnode *)real_malloc(sizeof(struct pointer_hnode)) ;
	if (!ptr_hnode)
		return ;

#ifdef DEBUG_MEMCHECK_RT
	{
		const int index = hash_min(ptr, HASH_BITS(pointer_info_hash_table)) ;
		RT_TRACE("new pointer info hash node: %d\n", index) ;
	}
#endif

	ptr_hnode->pointer = ptr ;
	ptr_hnode->record  = &mc_hnode->record ;
	ptr_hnode->size    = size ;
	hash_add(pointer_info_hash_table, &ptr_hnode->hnode, ptr) ;
}

static void memcheck_record_realloc(void *ptr, size_t size)
{
	struct malloc_record *record ;
	struct pointer_hnode *ptr_hnode ;

	ptr_hnode = pointer_hnode_find(ptr) ;
	if (!ptr_hnode)
	{
		RT_TRACE("can not find pointer in `realloc'! %p\n", ptr) ;
		return ;
	}

	/// NOTE: there is may have more than one `alloc stack' for the pointer
	/// because of `realloc', but only one will be recorded which is 
	/// `malloc stack'
	record = &ptr_hnode->record ;
	record->mem_size -= (ptr_hnode->size - size) ;
	ptr_hnode->size = size ;
}

static void memcheck_record_free(void *ptr)
{
	struct pointer_hnode *ptr_hnode ;
	struct malloc_record *record ;

	ptr_hnode = pointer_hnode_find(ptr) ;
	if (!ptr_hnode)
	{
#if 0
		// will never come here because double free will be catched by system
		int i ;
		void *stack_info[RT_MAX_STACK_SIZE] ;
		int stack_size ;
		char **symbols ;

		RT_TRACE("double free\n") ;

		stack_size = nohook_backtrace(stack_info, RT_STACK_SIZE) ;
		if (stack_size <= 0)
			return ;

		symbols = nohook_backtrace_symbols(stack_info, stack_size) ;
		
		for (i = 0 ; i < stack_size ; i++)
		{
			RT_TRACE("\t%s\n", symbols[i]) ;
		}
		RT_TRACE("\n") ;
		real_free(symbols) ;
#endif

		return ;
	}

	record = ptr_hnode->record ;
	record->mem_size -= ptr_hnode->size ;
	hash_del(&ptr_hnode->hnode) ;
	real_free(ptr_hnode) ;
}

static __attribute__((always_inline)) void memcheck_record(enum malloc_type type, void *ptr, size_t size)
{
	pthread_mutex_lock(&lock_record) ;

	switch (type)
	{
	default:
		RT_TRACE("unknown type to record: %p, %u\n", ptr, size) ;
		break ;

	case MT_MALLOC:
		memcheck_record_malloc(ptr, size) ;
		break ;
	case MT_REALLOC:
		memcheck_record_realloc(ptr, size) ;
		break ;
	case MT_FREE:
		memcheck_record_free(ptr) ;
		break ;
	};

	pthread_mutex_unlock(&lock_record) ;
}

static void sig_reocrd(int signo)
{
	if (record_fp)
		memcheck_dump_safe(record_fp) ;	
}

/// be careful use it in `gdb' in case that some threads may call memcheck_* 
/// routines which can cause thread unsafe events occurs
static void memcheck_dump(FILE *fp)
{
	struct memcheck_hnode *node ;
	struct malloc_record  *record ;
	int i, j ;

	RT_TRACE("[memcheck_rt] memcheck_dump begin\n") ;
	fprintf(fp, "-----------------------------------------------------------------"
		"---------------\n") ;

	hash_for_each(malloc_info_hash_table, i, node, hnode)
	{
		record = &node->record ;
		if (0 == record->mem_size)
			continue ;

		fprintf(fp, "memory size %d\n", record->mem_size) ;
		if (NULL == record->symbols)
		{
			record->symbols = nohook_backtrace_symbols(
				record->stack_info, record->stack_size) ;
		}
		if (record->symbols)
		{
			for (j = useful_stack_start ; j < record->stack_size ; j++)
			{
				fprintf(fp, "\t%014p:\t%s\n", record->stack_info[j], record->symbols[j]) ;
			}
			fprintf(fp, "\n") ;
		}
	}
	fprintf(fp, "\n") ;
	RT_TRACE("[memcheck_rt] memcheck_dump end\n") ;
}

static inline void memcheck_dump_safe(FILE *fp)
{
	pthread_mutex_lock(&lock_record) ;
	memcheck_dump(fp) ;
	pthread_mutex_unlock(&lock_record) ;
}

static void memcheck_dump_console()
{
	memcheck_dump(stdout);
}


///////////////////////////////////////////////////////////////////////////////
// hooks
#if 0		// not use anymore
inline void write_record(enum malloc_type type, void *ptr, size_t size)
{
	struct malloc_record record ;
	void *stack_info[RT_MAX_STACK_SIZE] ;
	struct iovec iov[2] ;

	if (!record_fp)
		return ;

	record.record_type = type ;
	record.stack_size = backtrace(stack_info, RT_STACK_SIZE) ;
	record.pointer = ptr ;
	record.mem_size = size ;

	iov[0].iov_base = &record ;
	iov[0].iov_len = sizeof(record) ;
	iov[1].iov_base = stack_info ;
	iov[1].iov_len = record.stack_size * sizeof(void *) ;
	RT_TRACE("stack size: %d\n", record.stack_size) ;

	pthread_mutex_lock(&lock_record) ;
	if (-1 == writev(record_fd, iov, 2) )
	{
		if (record_fd >= 0)
		{
			void *i[RT_MAX_STACK_SIZE] ;
			memset((void *)i, 0, sizeof(i) ) ;
			write(record_fd, i, sizeof(i) ) ;
			close(record_fd) ;
			record_fd = -1 ;
		}
	}
	pthread_mutex_unlock(&lock_record) ;
}
#endif

void (*volatile __malloc_initialize_hook) (void) = NULL ;
static __attribute__((constructor)) void memcheck_rt_init(void)
{
	__malloc_initialize_hook = rt_init_hook ;
	// pthread_mutex_init(&__lock, NULL) ;
}

static void rt_init_hook(void)
{
	const char *file_rt = getenv(RT_ENV_FILE) ;
	if (!file_rt)
		file_rt = RT_DEFAULT_FILE ;
	if (file_rt)
	{
		record_fp = fopen(file_rt, "ab+" ) ;
		if (!record_fp)
		{
			fprintf(stderr, "[memecheck_rt]: can not open %s\n", file_rt) ;
			exit(1) ;
		}
		fprintf(record_fp, "=============================================="
			"==================================\n") ;
		RT_TRACE("[memecheck_rt]: open file for record %s\n", file_rt) ;
	}

	assert(record_fp != NULL) ;

	int signo = 0 ;
	const char *sig = getenv(RT_ENV_SIGNO) ;
	if (sig)
		signo = atoi(sig) ;
	if (signo > 0)
	{
		struct sigaction sigact ;
		sigact.sa_handler = sig_reocrd ;
		sigemptyset(&sigact.sa_mask); 
		sigact.sa_flags = 0 ;
		sigaction(signo, &sigact, NULL) ;
	}

	old_malloc_hook = __malloc_hook ;
	old_realloc_hook = __realloc_hook ;
	old_free_hook = __free_hook ;
	__malloc_hook = rt_malloc_hook ;
	__realloc_hook = rt_realloc_hook ;
	__free_hook = rt_free_hook ;
}

static void *rt_malloc_hook(size_t size, const void *caller)
{
	void *result ;

	pthread_mutex_lock(&__lock) ;
	__malloc_hook = old_malloc_hook ;
	result = malloc(size) ;
	old_malloc_hook = __malloc_hook ;

	// restore
	__malloc_hook = rt_malloc_hook ;
	pthread_mutex_unlock(&__lock) ;

	// record
	memcheck_record(MT_MALLOC, result, size) ;

	return result ;
}

static void *rt_realloc_hook(void *ptr, size_t size, const void *caller)
{
	void *result ;

	pthread_mutex_lock(&__lock) ;
	__realloc_hook = old_realloc_hook ;
	result = realloc(ptr, size) ;
	old_realloc_hook = __realloc_hook ;
	// restore
	__realloc_hook = rt_realloc_hook ;
	pthread_mutex_unlock(&__lock) ;

	// record
	memcheck_record(MT_REALLOC, ptr, size) ;

	return result ;
}

static void rt_free_hook(void *ptr, const void *caller)
{
	pthread_mutex_lock(&__lock) ;
	__free_hook = old_free_hook ;
	free(ptr) ;
	old_free_hook = __free_hook ;

	// restore
	__free_hook = rt_free_hook ;
	pthread_mutex_unlock(&__lock) ;

	// record
	memcheck_record(MT_FREE, ptr, 0) ;
}

