/*
 * mm.c - Implicit Free List 기반 동적 메모리 할당자
 * 
 * 이 구현은 교과서의 implicit free list 방식을 사용합니다.
 * - 각 블록은 헤더와 푸터를 가지며, 크기와 할당 상태를 저장
 * - First-fit 방식으로 적합한 free 블록을 찾음 (방법 1)
 * - Boundary tag coalescing으로 인접한 free 블록들을 합침
 * - 블록이 충분히 클 경우 splitting을 통해 internal fragmentation 감소
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * 팀 정보
 ********************************************************/
team_t team = {
    /* Team name */
    "1team",
    /* First member's full name */
    "Taeyun Lee",
    /* First member's email address */
    "qwa7854@naver.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* =================================================================
 * 기본 정렬 상수 (기존 naive 방식과의 호환성을 위해 유지)
 * ================================================================= */
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* =================================================================
 * 가용 리스트 조작을 위한 기본 상수 및 매크로 정의
 * ================================================================= */

/* 기본 크기 상수들 */
#define WSIZE 4             /* 워드 크기: 헤더/푸터 크기 (4바이트) */
#define DSIZE 8             /* 더블 워드 크기: 최소 블록 크기 (8바이트) */
#define CHUNKSIZE (1 << 12) /* 힙 확장 단위: 한 번에 4096바이트씩 확장 */

/* 유틸리티 매크로 */
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* 헤더/푸터 값 생성: 크기와 할당 비트를 OR 연산으로 합침 */
#define PACK(size, alloc) ((size) | (alloc))

/* 메모리 읽기/쓰기: 주소 p에서 4바이트 정수 읽기/쓰기 */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* 헤더/푸터에서 정보 추출 */
#define GET_SIZE(p) (GET(p) & ~0x7)  /* 하위 3비트 제거해서 크기 추출 */
#define GET_ALLOC(p) (GET(p) & 0x1)  /* 최하위 비트로 할당 상태 확인 */

/* 블록 포인터 bp에서 헤더와 푸터 주소 계산 */
#define HDRP(bp) ((char *)(bp) - WSIZE)                           /* 헤더 = bp - 4 */
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)     /* 푸터 = bp + 블록크기 - 8 */

/* 현재 블록에서 다음/이전 블록의 payload 시작 주소 계산 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))  /* 다음 = bp + 현재블록크기 */
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))  /* 이전 = bp - 이전블록크기 */

/* =================================================================
 * 전역 변수
 * ================================================================= */
static char *heap_listp = 0;  /* 첫 번째 실제 블록의 payload를 가리키는 포인터 */

/* =================================================================
 * 내부 함수 프로토타입
 * ================================================================= */
static void *extend_heap(size_t words);    /* 힙 확장 */
static void *coalesce(void *bp);           /* 인접 블록 합치기 */
static void *find_fit(size_t asize);       /* 적합한 free 블록 찾기 */
static void place(void *bp, size_t asize); /* 블록 배치 및 분할 */

/*
 * mm_init - malloc 패키지 초기화
 * 
 * 초기 힙 구조:
 * [패딩][프롤로그헤더][프롤로그푸터][에필로그헤더] + [첫번째 free 블록]
 *   0       8/1        8/1        0/1
 */
int mm_init(void)
{
    /* 초기 빈 힙 생성: 4워드(16바이트) 할당 */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    /* 초기 힙 구조 설정 */
    PUT(heap_listp, 0);                          /* 패딩: 8바이트 정렬을 위한 더미 */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* 프롤로그 헤더: 8바이트, 할당됨 */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* 프롤로그 푸터: 8바이트, 할당됨 */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* 에필로그 헤더: 0바이트, 할당됨 */
    
    /* heap_listp가 첫 번째 실제 블록을 가리키도록 조정 */
    heap_listp += (2 * WSIZE);

    /* 힙을 CHUNKSIZE(4096)바이트 만큼 확장하여 첫 번째 free 블록 생성 */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/*
 * extend_heap - 힙을 새로운 free 블록으로 확장
 * @words: 확장할 워드 개수
 * 
 * 새로운 free 블록을 생성하고, 이전 블록이 free라면 coalescing 수행
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* 정렬 유지를 위해 짝수 개의 워드로 할당 */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    
    /* 시스템에서 메모리 할당 요청 */
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* 새로운 free 블록의 헤더/푸터 초기화 */
    PUT(HDRP(bp), PACK(size, 0));         /* 새 블록 헤더: size, free */
    PUT(FTRP(bp), PACK(size, 0));         /* 새 블록 푸터: size, free */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* 새로운 에필로그 헤더: 0, allocated */

    /* 이전 블록이 free라면 합치기 */
    return coalesce(bp);
}

/*
 * coalesce - Boundary tag coalescing으로 인접한 free 블록들 합치기
 * @bp: 합칠 블록의 포인터
 * 
 * 4가지 경우:
 * Case 1: [할당][현재][할당] → 합칠 블록 없음
 * Case 2: [할당][현재][자유] → 다음 블록과 합치기
 * Case 3: [자유][현재][할당] → 이전 블록과 합치기
 * Case 4: [자유][현재][자유] → 이전, 현재, 다음 모두 합치기
 */
static void *coalesce(void *bp)
{
    /* 이전/다음 블록의 할당 상태 확인 */
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));  /* 이전 블록 푸터에서 할당 상태 */
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));  /* 다음 블록 헤더에서 할당 상태 */
    size_t size = GET_SIZE(HDRP(bp));                     /* 현재 블록 크기 */

    if (prev_alloc && next_alloc) {            /* Case 1: 양쪽 모두 할당됨 */
        return bp;  /* 합칠 블록이 없으므로 그대로 반환 */
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2: 다음 블록만 free */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); /* 다음 블록 크기 추가 */
        PUT(HDRP(bp), PACK(size, 0));          /* 현재 블록 헤더 업데이트 */
        PUT(FTRP(bp), PACK(size, 0));          /* 새로운 푸터 위치에 업데이트 */
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3: 이전 블록만 free */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); /* 이전 블록 크기 추가 */
        PUT(FTRP(bp), PACK(size, 0));          /* 현재 블록 푸터 업데이트 */
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); /* 이전 블록 헤더 업데이트 */
        bp = PREV_BLKP(bp);                    /* 포인터를 이전 블록으로 이동 */
    }

    else {                                     /* Case 4: 양쪽 모두 free */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + /* 이전 블록 크기 + */
            GET_SIZE(FTRP(NEXT_BLKP(bp)));       /* 다음 블록 크기 */
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); /* 이전 블록 헤더 업데이트 */
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); /* 다음 블록 푸터 업데이트 */
        bp = PREV_BLKP(bp);                      /* 포인터를 이전 블록으로 이동 */
    }
    return bp;
}

/*
 * mm_malloc - 메모리 블록 할당
 * @size: 할당할 바이트 수
 * 
 * 할당 과정:
 * 1. 요청 크기를 블록 크기로 조정 (헤더/푸터 + 정렬 고려)
 * 2. First-fit으로 적합한 free 블록 찾기
 * 3. 못 찾으면 힙 확장 후 배치
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* 조정된 블록 크기 */
    size_t extendsize; /* 힙 확장이 필요한 경우의 확장 크기 */
    char *bp;

    /* 잘못된 요청 무시 */
    if (size == 0)
        return NULL;

    /* 블록 크기 조정: 헤더/푸터 오버헤드와 정렬 요구사항 포함 */
    if (size <= DSIZE)
        asize = 2 * DSIZE;  /* 최소 블록 크기: 16바이트 (헤더4 + 최소페이로드8 + 푸터4) */
    else
        /* 8바이트 배수로 정렬: (size + 헤더푸터8 + 7) / 8 * 8 */
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    /* free 리스트에서 적합한 블록 검색 */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);  /* 블록 배치 및 필요시 분할 */
        return bp;
    }

    /* 적합한 블록을 찾지 못한 경우: 힙 확장 */
    extendsize = MAX(asize, CHUNKSIZE);  /* 요청 크기 vs 기본 확장 크기 중 큰 값 */
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - 메모리 블록 해제
 * @ptr: 해제할 블록의 포인터
 * 
 * 해제 과정:
 * 1. 블록을 free로 표시 (헤더/푸터의 할당 비트를 0으로)
 * 2. 인접한 free 블록들과 coalescing 수행
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));  /* 블록 크기 추출 */

    /* 헤더와 푸터를 free로 표시 */
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    
    /* 인접한 free 블록들과 합치기 */
    coalesce(ptr);
}

/*
 * mm_realloc - 메모리 블록 재할당
 * @ptr: 기존 블록 포인터
 * @size: 새로운 크기
 * 
 * 간단한 구현: malloc + copy + free
 * 최적화 여지: 기존 블록 확장 가능성 검토
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    /* ptr이 NULL이면 malloc과 동일 */
    if (ptr == NULL)
        return mm_malloc(size);

    /* size가 0이면 free와 동일 */
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    /* 새로운 블록 할당 */
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    
    /* 기존 데이터 복사: payload 크기만 계산 (헤더/푸터 제외) */
    copySize = GET_SIZE(HDRP(oldptr)) - DSIZE;  /* 전체 블록 크기 - 헤더푸터 크기 */
    if (size < copySize)
        copySize = size;  /* 새 크기가 더 작으면 새 크기만큼만 복사 */
    
    memcpy(newptr, oldptr, copySize);  /* 데이터 복사 */
    mm_free(oldptr);                   /* 기존 블록 해제 */
    return newptr;
}

/*
 * find_fit - First-fit 방식으로 적합한 free 블록 찾기
 * @asize: 필요한 블록 크기
 * 
 * 힙을 처음부터 순회하면서 다음 조건을 만족하는 첫 번째 블록 반환:
 * 1. free 상태 (!GET_ALLOC)
 * 2. 요청 크기보다 크거나 같음 (asize <= block_size)
 */
static void *find_fit(size_t asize)
{
    void *bp;

    /* 힙 순회: heap_listp부터 에필로그 헤더(크기 0)까지 */
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        /* free 블록이고 크기가 충분한지 확인 */
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;  /* 첫 번째 적합한 블록 반환 */
        }
    }
    return NULL;  /* 적합한 블록을 찾지 못함 */
}

/*
 * place - 찾은 free 블록에 요청된 크기만큼 할당하고 필요시 분할
 * @bp: 할당할 free 블록
 * @asize: 할당할 크기
 * 
 * 분할 조건: 남은 공간이 최소 블록 크기(16바이트) 이상이면 분할
 * - 분할 O: [할당된블록][새로운free블록]
 * - 분할 X: [전체블록할당] (internal fragmentation 발생하지만 관리 오버헤드 없음)
 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));  /* 현재 블록의 전체 크기 */

    /* 분할 가능한지 확인: 남은 공간이 최소 블록 크기(2*DSIZE=16) 이상인가? */
    if ((csize - asize) >= (2 * DSIZE)) {
        /* 분할 수행 */
        
        /* 첫 번째 부분: 요청된 크기만큼 할당 */
        PUT(HDRP(bp), PACK(asize, 1));  /* 할당된 블록 헤더 */
        PUT(FTRP(bp), PACK(asize, 1));  /* 할당된 블록 푸터 */
        
        /* 두 번째 부분: 남은 공간을 새로운 free 블록으로 */
        bp = NEXT_BLKP(bp);                        /* 다음 블록으로 이동 */
        PUT(HDRP(bp), PACK(csize - asize, 0));     /* 새 free 블록 헤더 */
        PUT(FTRP(bp), PACK(csize - asize, 0));     /* 새 free 블록 푸터 */
    }
    else {
        /* 분할하지 않고 전체 블록 할당 */
        PUT(HDRP(bp), PACK(csize, 1));  /* 전체 블록 할당으로 표시 */
        PUT(FTRP(bp), PACK(csize, 1));
    }
}