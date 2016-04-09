#ifndef _MEMCHECK_RT_H
#define _MEMCHECK_RT_H

#define RT_MAX_STACK_SIZE  (2 << 6)
#define RT_STACK_SIZE 20

#define MEMCHECK_RT_FILE_ENV    "MEMCHECK_RT"
#define MEMCHECK_RT_DEFAULT_FILE "memcheck_rt.log"

enum malloc_type
{
	MT_MALLOC    = 0x01 ,
	MT_REALLOC   = 0x02 ,
	MT_FREE      = 0x03 ,
} ;
struct malloc_record
{
	int     record_type : 2 ;
	int     stack_size  : 6;
	int     mem_size    : 24 ;
	void ** stack_info ;
	char ** symbols ;
};

#endif // _MEMCHECK_RT_H

