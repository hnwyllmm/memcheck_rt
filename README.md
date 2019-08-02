平台 Linux，GCC
GLIBC version 2.17
(backtrace(), backtrace_symbols(), and backtrace_symbols_fd() are provided in glibc since version 2.1.)

# 简介
很多内存泄露检测工具都是在进程退出时才汇报内存泄露的位置。但是有很多泄露是在运行中发生的，并且在进程退出时检测不到，比如使用vector，运行时不停的push_back，内存肯定会迅速增长，但是在进程退出时，vector析构，内存是没有泄露的。
这个工具就是参考Solaris的libumem工具，在进程运行过程中监控内存申请和释放，通过对比两次内存申请释放的“快照”来发现内存泄露。创建“快照”的方法是通过给进程发送信号。

# 怎么使用
LD_PRELOAD="libmemcheck_rt.so" a.out

创建内存分配信息“快照”:
向进程发送信号
示例:
> ps -ef | grep memcheck

hnwyllmm  1775  1397  0 10:54 pts/1    00:00:00 ./memcheck_rt_thread.test

> kill -16 1775

默认信号是16，可以通过环境变量`RT_ENV_SIGNO`来配置。
记录文件可以通过`RT_ENV_FILE`来配置。
