SOURCE = memcheck_rt.c			\
		 memcheck_rt_noth.c		\
		 memcheck_rt_thread.c

CC=gcc
# CXX= g++
CFLAG= -g -O0 -c -m64 -fPIC -rdynamic
LDFLAG= -g -O0 -m64 -rdynamic -ldl
DEFS=DEBUG_MEMCHECK_RT

TARGET = libmemcheck_rt.so			\
		 memcheck_rt_noth.test		\
		 memcheck_rt_thread.test


#		 mc_analyser

all:$(TARGET)

# libary
libmemcheck_rt.so:memcheck_rt.o
	$(CC) -shared -fPIC -o $@ $(LDFLAG) -lpthread $<

# test for multi-thread
memcheck_rt_thread.test:libmemcheck_rt.so memcheck_rt_thread.o
	$(CC) -o $@ $(LDFLAG) memcheck_rt_thread.o -L. -lpthread

# exe
# mc_analyser:mc_analyser.o
# # $(CC) -o $@ $(LDFLAG) $<


# INCLUDE=$(OB_REL)/include

DEFS+=LINUX

# for normal


LD_LIBARY=$(addprefix -l, $(PREDPEND))
INCLUDE_PATHS=$(addprefix -I, $(INCLUDE))
LIBARY_PATHS=$(addprefix -L, $(LIBARY))
MACRO_DEFS=$(addprefix -D, $(DEFS))

OBJECT=$(SOURCE:.c=.o)
DEPEND=$(SOURCE:.c=.d)

$(DEPEND):%.d:%.c
	$(CC) $(INCLUDE_PATHS) -MM $< > $@;	\
	echo "\t$(CC) $(CFLAG) $(INCLUDE_PATHS) $(MACRO_DEFS) $< -o $(@:.d=.o)" >> $@


-include $(DEPEND)

.PHONY: clean
clean:
	-rm -f $(TARGET) $(OBJECT) $(DEPEND) core*

