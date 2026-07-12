#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>

void test() {
    SceUID memid = sceKernelAllocMemBlock("video_mem", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 1024*1024, NULL);
}
