/*

使用分离空闲链表，以2的幂作为大小类来存储，需要25类，每类存一个指针，需要SIZE_T_SIZE字节
在堆空间中的每个块都需要一个头部存它的大小，可以使用4字节来表示大小和有效位
每个块也需要一个尾部,4字节
对于空闲块，需要两个指针，分别指向前一个空闲块和后一个空闲块，需要2×size_t字节
故空闲块一共是8+2*size_t字节，
小于这个数的空闲块不列入空闲链表，至少要有8个字节来存头部和尾部（不确定这种最小的情况是否存在）
对于不空闲的块，只需要8字节的头尾即可
对于整个空间来说，开头需要放四个字节来标志一下作为头，结尾需要四个字节来标志一下作为尾

调用free的时候检查空闲块并合并

 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "jteam",
    /* First member's full name */
    "Jay cui",
    /* First member's email address */
    "csj@jaycui.one", "", ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// global varibles
char *mem_start;
char *store_start;

// my define micro
#define ARRSIZE (SIZE_T_SIZE * 25)
#define MINSIZE (8 + SIZE_T_SIZE * 2)
#define FBLOCKSIZE(x) (ALIGN(x + MINSIZE))
#define BLOCKSIZE(x) (ALIGN(x + 8))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GETSIZE(p) (GET(p) & ~0x7)
#define PUT_END(p, val) (*(unsigned int *)(p + GETSIZE(p) - 4) = val)
#define GETALLO(p) (GET(p) & 0x1)
#define GET_PRE(p) (*(char **)(p + 4))
#define GET_NEXT(p) (*(char **)(p + 4 + SIZE_T_SIZE))
#define PUT_PRE(p, val) (*(char **)(p + 4) = val)
#define PUT_NEXT(p, val) (*(char **)(p + 4 + SIZE_T_SIZE) = val)

/*
  定义处理分离空闲链表的方法
*/
void insertclass(char *newp, unsigned int size, int num) {
  char *tmp = ((char **)mem_start)[num];
  char *end;
  if (tmp == NULL) {
    ((char **)mem_start)[num] = newp;
    PUT_PRE(newp, NULL);
    PUT_NEXT(newp, NULL);
    PUT(newp, size);
    PUT_END(newp, size);
    return;
  }
  while (tmp != NULL) {
    if (GETSIZE(tmp) > num) {
      char *pre = GET_PRE(tmp);
      if (pre != NULL) {
        PUT_NEXT(pre, newp);
      } else {
        ((char **)mem_start)[num] = newp;
      }
      PUT_PRE(newp, pre);
      PUT_NEXT(newp, tmp);
      PUT_PRE(tmp, newp);
      PUT(newp, size);
      PUT_END(newp, size);
      return;
    }
    end = tmp;
    tmp = GET_NEXT(tmp);
  }
  if (tmp == NULL) {
    PUT_PRE(newp, end);
    PUT_NEXT(end, newp);
    PUT_NEXT(newp, NULL);
    PUT(newp, size);
    PUT_END(newp, size);
    return;
  }
}

void dropclass(char *ptr, int num) {
  if (GETSIZE(ptr) < 8 + 2 * SIZE_T_SIZE) {
    return;
  }
  char *pre = GET_PRE(ptr);
  char *next = GET_NEXT(ptr);
  if (pre == NULL) {
    ((char **)mem_start)[num] = next;
  } else {
    PUT_NEXT(pre, next);
  }
  if (next != NULL) {
    PUT_PRE(next, pre);
  }
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
  mem_init();
  mem_start = mem_heap_lo();
  mem_sbrk(ARRSIZE + 8); // 8 用来标识开头结尾，设置为1
  store_start = (char *)mem_heap_hi() - 7;
  if (mem_start == store_start) {
    return -1;
  } else {
    for (char *tmp = mem_start; tmp < store_start; tmp += SIZE_T_SIZE) { 
      *(char **)tmp = NULL;
    } // here to init into NULL
  }
  PUT(store_start, 1);
  PUT((store_start + 4), 1);
  return 0;
}
int getclassnum(unsigned int size) {
  int num = 0;
  while ((size >>= 1) > 0) {
    num++;
  }
  return num;
}

void findgoodplace(char *newp, unsigned int size) {
  if (size < (8 + 2 * SIZE_T_SIZE)) {
    PUT(newp, size);
    PUT_END(newp, size);
    return;
  }
  int num = getclassnum(size);
  insertclass(newp, size, num);
}

/*

 */
void *mm_malloc(size_t size) {
  if (size == 0) {
    return NULL;
  } // we donnot do this
  size_t newsize = BLOCKSIZE(size);
  int num = getclassnum(newsize);
  while (num < 25) {
    char *tmp = ((char **)mem_start)[num];
    while (tmp != NULL) {
      unsigned int ns = GETSIZE(tmp);
      if (ns >= newsize) {
        int tnum = getclassnum(ns);
        dropclass(tmp, tnum);
        unsigned int usi_cp = newsize;
        newsize |= 1;
        PUT(tmp, newsize);
        PUT_END(tmp, newsize);
        if (ns > usi_cp) {
          char *newp = tmp + usi_cp;
          ns -= usi_cp;
          findgoodplace(newp, ns);
        }
        return tmp + 4;
      } // find it
      tmp = GET_NEXT(tmp);
    }
    num++;
  } // find useless block
  char *newpp = mem_sbrk(newsize);
  if (newpp == (void *)-1) {
    return NULL;
  }
  newpp -= 4; // 减去之前标志结尾的4字节
  newsize |= 1;
  PUT(newpp, newsize);
  PUT_END(newpp, newsize);
  char *endpoint = (char *)mem_heap_hi() - 3;
  PUT(endpoint, 1);
  return newpp + 4; // no useless block, so we must new one
}

/*

 */
void mm_free(void *ptr) {
  char *start = ((char *)ptr - 4);
  unsigned int size = GETSIZE(start);
  int bit = GETALLO(start);
  if (bit != 1) {
    printf("WRONG FREE\n");
    exit(-1);
  }
  char *next = start + size;
  char *pre = start - 4;
  if (GETALLO(pre) == 0) {
    pre = start - GETSIZE(pre);
    dropclass(pre, getclassnum(GETSIZE(pre)));
    start -= (GETSIZE(pre));
    size += GETSIZE(pre);
  }
  if (GETALLO(next) == 0) {
    dropclass(next, getclassnum(GETSIZE(next)));
    size += GETSIZE(next);
  }
  findgoodplace(start, size);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return mm_malloc(size);
  }
  char *old_start = (char *)ptr - 4;
  unsigned int old_size = GETSIZE(old_start);
  unsigned int new_size = BLOCKSIZE(size);
  if (old_size > new_size) {
    PUT(old_start, (new_size | 1));
    PUT_END(old_start, (new_size | 1));
    char *new_start = old_start + new_size;
    findgoodplace(new_start, old_size - new_size);
    return old_start + 4;
  }
  char *newptr = mm_malloc(size);
  if (newptr == NULL) {
    return NULL;
  }
  memcpy(newptr, ptr, old_size - 8);
  mm_free(ptr);
  return newptr;
}
