#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
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
#define ARRSIZE (SIZE_T_SIZE * 21)
#define MINSIZE (8 + SIZE_T_SIZE * 2)
#define BLOCKSIZE(x) (ALIGN(x + 8))
#define GET(p) (*(unsigned int *)(p))
#define GETALLO(p) (GET(p) & 0x1)
#define GETSIZE(p) (GET(p) & ~0x7)
// 处理头部和尾部，放置尾部前必须先放置头部
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define PUT_END(p, val) (*(unsigned int *)(p + GETSIZE(p) - 4) = val)
// 用于处理空闲块的指针
#define GET_PRE(p) (*(char **)(p + 4))
#define GET_NEXT(p) (*(char **)(p + 4 + SIZE_T_SIZE))
#define PUT_PRE(p, val) (*(char **)(p + 4) = val)
#define PUT_NEXT(p, val) (*(char **)(p + 4 + SIZE_T_SIZE) = val)

/*
  定义处理分离空闲链表的方法
*/

/*
将newp块插入num这个大小类中，并会设置好newp的头部和尾部
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


/*
在num这个大小类中删除ptr
*/
void dropclass(char *ptr, int num) {
  if (num == -1) {
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
utils
*/

/*
根据size的大小确定属于分离空闲链表中的哪个大小类
*/
int getclassnum(unsigned int size) {
  if (size < (8 + 2 * SIZE_T_SIZE)) {
    return -1;
  }
  int num = 0;
  while ((size >>= 1) > 0) {
    num++;
  }
  return num - 4;
}

/*
在分离空闲链表中找到最合适的地方，放置newp
*/
void findgoodplace(char *newp, unsigned int size) {
  int num = getclassnum(size);
  if (num == -1) {
    PUT(newp, size);
    PUT_END(newp, size);
    return;
  }
  insertclass(newp, size, num);
}

/*
找到分离空闲链表中大于size的第一个空闲块
*/
char *findbestblock(unsigned int size) {
  int num = getclassnum(size);
  while (num < 21) {
    if (num == -1) {
      return NULL;
    }
    char *tmp = ((char **)mem_start)[num];
    while (tmp != NULL) {
      unsigned int ns = GETSIZE(tmp);
      if (ns >= size) {
        return tmp;
      }
      tmp = GET_NEXT(tmp);
    } // find it
    num++;
  }
  return NULL;
}

/*
  拓展堆newsize的大小，但是如果最后一个块是空闲的，则其大小算在newsize内
  返回值为最后一个块的数据开头，且块已经标记为占用
*/
char *extendheap(unsigned int newsize) {
  char *endpp = (char *)mem_heap_hi() - 7;
  int vid = GETALLO(endpp);
  if (vid == 0) {
    // 最后一个块空闲
    unsigned int endsize = GETSIZE(endpp);
    endpp = endpp - endsize + 4;
    dropclass(endpp, getclassnum(endsize));
    unsigned int less_size = newsize - endsize;
    mem_sbrk(less_size);
    PUT(endpp, newsize | 1);
    PUT_END(endpp, newsize | 1);
    char *endpoint = (char *)mem_heap_hi() - 3;
    PUT(endpoint, 1);
    return endpp + 4;
  } else {
    // 最后一个块不空闲
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
    return newpp + 4;
  }
}

/*
 * mm_init - 构建分离空闲链表，并初始化堆的开头和结尾
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

/*
mm_malloc 在分离空闲链表中查找合适空闲块，没找到就拓展堆
 */
void *mm_malloc(size_t size) {
  if (size == 0) {
    return NULL;
  } // we donnot do this
  size_t newsize = BLOCKSIZE(size);

  // 在空闲链表种子找到最适合的块
  char *tmp = findbestblock(newsize);
  if (tmp != NULL) {
    unsigned int ns = GETSIZE(tmp);
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
  }
  // 没找到空闲块
  return extendheap(newsize);
}

/*
mm_free free时检查前后块是否空，空的话合并
 */
void mm_free(void *ptr) {
  char *start = ((char *)ptr - 4);
  unsigned int size = GETSIZE(start);
  int bit = GETALLO(start);
  if (bit != 1) {
    printf("WRONG FREE\n");
    exit(-1);
  }
  // 检查前后块并合并
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
 * mm_realloc, 将原块视作空闲块，模仿malloc函数
 */
void *mm_realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return mm_malloc(size);
  }
  // 此处我们先计算一下被realloc的块加上周围空闲块的大小，但是先不将周围空闲块drop
  char *start = ptr - 4;
  unsigned int oldsize = GETSIZE(start);
  unsigned int newsize = BLOCKSIZE(size);
  if (oldsize == newsize) {
    return ptr;
  }
  char *pre = start - 4;
  char *next = start + oldsize;
  unsigned int copysize = size < (oldsize - 8) ? size : (oldsize - 8);
  if (GETALLO(pre) == 0) {
    oldsize += GETSIZE(pre);
    start -= GETSIZE(pre);
    // dropclass(start, getclassnum(GETSIZE(pre)));
  }
  if (GETALLO(next) == 0) {
    oldsize += GETSIZE(next);
    // dropclass(next, getclassnum(GETSIZE(next)));
  }
  // 这里获得空闲链表中的最合适空闲块
  char *best = findbestblock(newsize);

  // 和malloc一样，分找到空闲块和没找到空闲块两种
  if (best != NULL) {
    // 找到空闲块
    // 考虑找到的空闲块和原块的大小区别
    unsigned int bestsize = GETSIZE(best);
    if (bestsize < oldsize || oldsize < newsize) {
      // 放在best里
      dropclass(best, getclassnum(bestsize));
      PUT(best, newsize | 1);
      PUT_END(best, newsize | 1);
      memcpy(best + 4, ptr, copysize);
      findgoodplace(best + newsize, bestsize - newsize);
      // 把malloc的块free掉
      mm_free(ptr);
      return best + 4;
    } else {
      // 放在malloc块里
      if (GETALLO(pre) == 0) {
        dropclass((pre - GETSIZE(pre) + 4), getclassnum(GETSIZE(pre)));
      }
      if (GETALLO(next) == 0) {
        dropclass(next, getclassnum(GETSIZE(next)));
      }
      memcpy(start + 4, ptr, copysize);
      PUT(start, newsize | 1);
      PUT_END(start, newsize | 1);
      findgoodplace(start + newsize, oldsize - newsize);
      return start + 4;
    }
  } else {
    // 没找到空闲块
    // 则有两个情况，一个是malloc的块满足，一个是我们要继续开块
    if (oldsize > newsize) {
      // malloc 块满足
      if (GETALLO(pre) == 0) {
        dropclass((pre - GETSIZE(pre) + 4), getclassnum(GETSIZE(pre)));
      }
      if (GETALLO(next) == 0) {
        dropclass(next, getclassnum(GETSIZE(next)));
      }
      memcpy(start + 4, ptr, copysize);
      PUT(start, newsize | 1);
      PUT_END(start, newsize | 1);
      findgoodplace(start + newsize, oldsize - newsize);
      return start + 4;
    } else {
      // 需要继续开块，开块的时候我们需要考虑malloc的块是不是在最后
      char *maybeend = start + oldsize;
      if (GETALLO(maybeend) == 1 && GETSIZE(maybeend) == 0) {
        // 说明在最后,放在malloc块中
        if (GETALLO(pre) == 0) {
          dropclass((pre - GETSIZE(pre) + 4), getclassnum(GETSIZE(pre)));
        }
        if (GETALLO(next) == 0) {
          dropclass(next, getclassnum(GETSIZE(next)));
        }
        mem_sbrk(newsize - oldsize);
        memcpy(start + 4, ptr, copysize);
        PUT(start, newsize | 1);
        PUT_END(start, newsize | 1);
        char *endpoint = (char *)mem_heap_hi() - 3;
        PUT(endpoint, 1);
        return start + 4;
      } else {
        // 此处说明不在最后，直接拓展
        char *new = extendheap(newsize);
        memcpy(new, ptr, copysize);
        mm_free(ptr);
        return new;
      }
    }
  }
}