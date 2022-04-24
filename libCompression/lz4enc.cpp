#include "lz4enc.h"

#ifdef _DEBUG
#define LZ4_DEBUG 3//16
#else
#define LZ4_DEBUG 2
#endif

#define LZ4_HEAPMODE 1

#pragma region Copied from lz4.h
#ifndef LZ4_MEMORY_USAGE
# define LZ4_MEMORY_USAGE 14
#endif

typedef union LZ4_stream_u LZ4_stream_t; /* incomplete type (defined later) */
typedef struct LZ4_stream_t_internal LZ4_stream_t_internal;

#define LZ4_HASHLOG   (LZ4_MEMORY_USAGE-2)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG) /* required as macro for static allocation */

struct LZ4_stream_t_internal {
    uint32_t hashTable[LZ4_HASH_SIZE_U32];
    uint32_t currentOffset;
    uint16_t initCheck;
    uint16_t tableType;
    const uint8_t* dictionary;
    const LZ4_stream_t_internal* dictCtx;
    uint32_t dictSize;
};

/*!
 * LZ4_stream_t :
 * information structure to track an LZ4 stream.
 * init this structure before first use.
 * note : only use in association with static linking !
 *        this definition is not API/ABI safe,
 *        it may change in a future version !
 */
#define LZ4_STREAMSIZE_U64 ((1 << (LZ4_MEMORY_USAGE-3)) + 4)
#define LZ4_STREAMSIZE     (LZ4_STREAMSIZE_U64 * sizeof(unsigned long long))
union LZ4_stream_u {
    unsigned long long table[LZ4_STREAMSIZE_U64];
    LZ4_stream_t_internal internal_donotuse;
};

/*-************************************
*  Advanced Functions
**************************************/
#define LZ4_MAX_INPUT_SIZE        0x7E000000   /* 2 113 929 216 bytes */
#define LZ4_COMPRESSBOUND(isize) ((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize)/255) + 16)

#pragma endregion Copied from lz4.h

#pragma region Misc

#define ACCELERATION_DEFAULT 1

/*-************************************
*  Compiler Options
**************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  include <intrin.h>
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4293)        /* disable: C4293: too large shift (32-bits) */
#endif  /* _MSC_VER */

#ifndef LZ4_FORCE_INLINE
#  ifdef _MSC_VER    /* Visual Studio */
#    define LZ4_FORCE_INLINE static __forceinline
#  else
#    if defined (__cplusplus) || defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#      ifdef __GNUC__
#        define LZ4_FORCE_INLINE static inline __attribute__((always_inline))
#      else
#        define LZ4_FORCE_INLINE static inline
#      endif
#    else
#      define LZ4_FORCE_INLINE static
#    endif /* __STDC_VERSION__ */
#  endif  /* _MSC_VER */
#endif /* LZ4_FORCE_INLINE */

#if defined(__PPC64__) && defined(__LITTLE_ENDIAN__) && defined(__GNUC__)
#  define LZ4_FORCE_O2_GCC_PPC64LE __attribute__((optimize("O2")))
#  define LZ4_FORCE_O2_INLINE_GCC_PPC64LE __attribute__((optimize("O2"))) LZ4_FORCE_INLINE
#else
#  define LZ4_FORCE_O2_GCC_PPC64LE
#  define LZ4_FORCE_O2_INLINE_GCC_PPC64LE static
#endif

/*-************************************
*  Basic Types
**************************************/
#if defined(__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
# include <stdint.h>
  typedef  uint8_t BYTE;
  typedef uint16_t U16;
  typedef uint32_t U32;
  typedef  int32_t S32;
  typedef uint64_t U64;
  typedef uintptr_t uptrval;
#else
  typedef unsigned char       BYTE;
  typedef unsigned short      U16;
  typedef unsigned int        U32;
  typedef   signed int        S32;
  typedef unsigned long long  U64;
  typedef size_t              uptrval;   /* generally true, except OpenVMS-64 */
#endif

/*-************************************
*  Error detection
**************************************/
#if defined(LZ4_DEBUG) && (LZ4_DEBUG>=3) //>=1
#  include <assert.h>
#else
#  ifndef assert
#    define assert(condition) ((void)0)
#  endif
#endif

#define LZ4_STATIC_ASSERT(c)   { enum { LZ4_static_assert = 1/(int)(!!(c)) }; }   /* use after variable declarations */

#if defined(LZ4_DEBUG) && (LZ4_DEBUG>=2)
#  include <stdio.h>
static int g_debuglog_enable = 1;
#  define DEBUGLOG(l, ...) {                                  \
                if ((g_debuglog_enable) && (l<=LZ4_DEBUG)) {  \
                    fprintf(stderr, __FILE__ ": ");           \
                    fprintf(stderr, __VA_ARGS__);             \
                    fprintf(stderr, " \n");                   \
            }   }
#else
#  define DEBUGLOG(l, ...)      {}    /* disabled */
#endif
  
#if (defined(__GNUC__) && (__GNUC__ >= 3)) || (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 800)) || defined(__clang__)
#  define expect(expr,value)    (__builtin_expect ((expr),(value)) )
#else
#  define expect(expr,value)    (expr)
#endif

#ifndef likely
#define likely(expr)     expect((expr) != 0, 1)
#endif
#ifndef unlikely
#define unlikely(expr)   expect((expr) != 0, 0)
#endif

/*-************************************
*  Memory routines
**************************************/
#include <stdlib.h>   /* malloc, calloc, free */
#define ALLOC(s)          malloc(s)
#define ALLOC_AND_ZERO(s) calloc(1,s)
#define FREEMEM(p)        free(p)
#include <string.h>   /* memset, memcpy */
#define MEM_INIT(p,v,s) memset((p),(v),(s))

/*-************************************
*  Common Constants
**************************************/
#define MINMATCH 4
#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (WILDCOPYLENGTH+MINMATCH)
static const int LZ4_minLength = (MFLIMIT+1);
#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)
#define MAXD_LOG 16
#define MAX_DISTANCE ((1 << MAXD_LOG) - 1)
#define ML_BITS  4
#define ML_MASK  ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)
static const int LZ4_64Klimit = ((64 KB) + (MFLIMIT-1));
static const U32 LZ4_skipTrigger = 6; /* Increase this value ==> compression run slower on incompressible data */

typedef enum { noDict = 0, withPrefix64k, usingExtDict, usingDictCtx } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { notLimited = 0, limitedOutput = 1, fillOutput = 2 } limitedOutput_directive;
typedef enum { clearedTable = 0, byPtr, byU32, byU16 } tableType_t;

#if defined(__x86_64__)
  typedef U64    reg_t;   /* 64-bits in x32 mode */
#else
  typedef size_t reg_t;   /* 32-bits in x32 mode */
#endif

static unsigned LZ4_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental */
    return one.c[0];
}
#pragma endregion Misc

void LZ4_resetStream (LZ4_stream_t* LZ4_stream)
{
    DEBUGLOG(5, "LZ4_resetStream (ctx:%p)", LZ4_stream);
    MEM_INIT(LZ4_stream, 0, sizeof(LZ4_stream_t));
}

static unsigned LZ4_NbCommonBytes (reg_t val)
{
    if (LZ4_isLittleEndian()) {
        if (sizeof(val)==8) {
#       if defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanForward64( &r, (U64)val );
            return (int)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_ctzll((U64)val) >> 3);
#       else
            static const int DeBruijnBytePos[64] = { 0, 0, 0, 0, 0, 1, 1, 2,
                                                     0, 3, 1, 3, 1, 4, 2, 7,
                                                     0, 2, 3, 6, 1, 5, 3, 5,
                                                     1, 3, 4, 4, 2, 5, 6, 7,
                                                     7, 0, 1, 2, 3, 3, 4, 6,
                                                     2, 6, 5, 5, 3, 4, 5, 6,
                                                     7, 1, 2, 4, 6, 4, 4, 5,
                                                     7, 2, 6, 5, 7, 6, 7, 7 };
            return DeBruijnBytePos[((U64)((val & -(long long)val) * 0x0218A392CDABBD3FULL)) >> 58];
#       endif
        } else /* 32 bits */ {
#       if defined(_MSC_VER) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r;
            _BitScanForward( &r, (U32)val );
            return (int)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_ctz((U32)val) >> 3);
#       else
            static const int DeBruijnBytePos[32] = { 0, 0, 3, 0, 3, 1, 3, 0,
                                                     3, 2, 2, 1, 3, 2, 0, 1,
                                                     3, 3, 1, 2, 2, 2, 2, 0,
                                                     3, 1, 2, 0, 1, 0, 1, 1 };
            return DeBruijnBytePos[((U32)((val & -(S32)val) * 0x077CB531U)) >> 27];
#       endif
        }
    } else   /* Big Endian CPU */ {
        if (sizeof(val)==8) {   /* 64-bits */
#       if defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanReverse64( &r, val );
            return (unsigned)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_clzll((U64)val) >> 3);
#       else
            static const U32 by32 = sizeof(val)*4;  /* 32 on 64 bits (goal), 16 on 32 bits.
                Just to avoid some static analyzer complaining about shift by 32 on 32-bits target.
                Note that this code path is never triggered in 32-bits mode. */
            unsigned r;
            if (!(val>>by32)) { r=4; } else { r=0; val>>=by32; }
            if (!(val>>16)) { r+=2; val>>=8; } else { val>>=24; }
            r += (!val);
            return r;
#       endif
        } else /* 32 bits */ {
#       if defined(_MSC_VER) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanReverse( &r, (unsigned long)val );
            return (unsigned)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_clz((U32)val) >> 3);
#       else
            unsigned r;
            if (!(val>>16)) { r=2; val>>=8; } else { r=0; val>>=24; }
            r += (!val);
            return r;
#       endif
        }
    }
}

static U16 LZ4_read16(const void* memPtr) { return *(const U16*) memPtr; }
static U32 LZ4_read32(const void* memPtr) { return *(const U32*) memPtr; }
static reg_t LZ4_read_ARCH(const void* memPtr) { return *(const reg_t*) memPtr; }
static U32 LZ4_hash4(U32 sequence, tableType_t const tableType)
{
    if (tableType == byU16)
        return ((sequence * 2654435761U) >> ((MINMATCH*8)-(LZ4_HASHLOG+1)));
    else
        return ((sequence * 2654435761U) >> ((MINMATCH*8)-LZ4_HASHLOG));
}

static U32 LZ4_hash5(U64 sequence, tableType_t const tableType)
{
    static const U64 prime5bytes = 889523592379ULL;
    static const U64 prime8bytes = 11400714785074694791ULL;
    const U32 hashLog = (tableType == byU16) ? LZ4_HASHLOG+1 : LZ4_HASHLOG;
    if (LZ4_isLittleEndian())
        return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
    else
        return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
}
LZ4_FORCE_INLINE U32 LZ4_hashPosition(const void* const p, tableType_t const tableType)
{
    if ((sizeof(reg_t)==8) && (tableType != byU16)) return LZ4_hash5(LZ4_read_ARCH(p), tableType);
    return LZ4_hash4(LZ4_read32(p), tableType);
}
static void LZ4_putPositionOnHash(U32 offset, U32 h,
                                  void* tableBase, tableType_t const tableType)
{
    switch (tableType)
    {
    default: /* fallthrough */
    case clearedTable: /* fallthrough */
    case byPtr: { /* illegal! */ assert(0); return; }
    case byU32: { U32* hashTable = (U32*) tableBase; hashTable[h] = (U32)(offset); return; }
    case byU16: { U16* hashTable = (U16*) tableBase; assert(offset < 65536); hashTable[h] = (U16)(offset); return; }
    }
}

static void LZ4_putIndexOnHash(U32 idx, U32 h, void* tableBase, tableType_t const tableType)
{
    switch (tableType)
    {
    default: /* fallthrough */
    case clearedTable: /* fallthrough */
    case byPtr: { /* illegal! */ assert(0); return; }
    case byU32: { U32* hashTable = (U32*) tableBase; hashTable[h] = idx; return; }
    case byU16: { U16* hashTable = (U16*) tableBase; assert(idx < 65536); hashTable[h] = (U16)idx; return; }
    }
}

LZ4_FORCE_INLINE void LZ4_putPosition(const void* const p, U32 offset, void* tableBase, tableType_t tableType)
{
    U32 const h = LZ4_hashPosition(p, tableType);
    LZ4_putPositionOnHash(offset, h, tableBase, tableType);
}

static U32 LZ4_getIndexOnHash(U32 h, const void* tableBase, tableType_t tableType)
{
    LZ4_STATIC_ASSERT(LZ4_MEMORY_USAGE > 2);
    if (tableType == byU32) {
        const U32* const hashTable = (const U32*) tableBase;
        assert(h < (1U << (LZ4_MEMORY_USAGE-2)));
        return hashTable[h];
    }
    if (tableType == byU16) {
        const U16* const hashTable = (const U16*) tableBase;
        assert(h < (1U << (LZ4_MEMORY_USAGE-1)));
        return hashTable[h];
    }
    assert(0); return 0;  /* forbidden case */
}

//Returns the bytes read.
static int LZ4e_inputBuffer(BYTE* istart, BYTE*& ip, BYTE* imax, LZ4e_instream_t* inStream)
{
	//Keep enough bytes for the dictionary.
	uint64_t absIPOffset = ip-istart + inStream->pos;
	int baseOffset = (imax - istart) / 2; 
	if (baseOffset > LZ4_64Klimit) baseOffset = LZ4_64Klimit;
	if (baseOffset > absIPOffset) baseOffset = absIPOffset;

	inStream->pos = absIPOffset - baseOffset;
	int len = inStream->callback(istart, imax-istart, inStream);

	assert(len >= baseOffset);
	if (unlikely(len < baseOffset))
		ip = istart + len; //safety
	else
		ip = istart + baseOffset;

	return len;
}
//Returns the bytes written.
static int LZ4e_outputBuffer(BYTE* ostart, BYTE*& op, BYTE* omax, LZ4e_outstream_t* outStream)
{
	int len = outStream->callback(ostart, op-ostart, outStream);
	assert(len == op-ostart);
	op = ostart;
	return len;
}
static U32 LZ4e_Read32Stream(unsigned int pos, LZ4e_instream_t* inStream, int *error)
{
	uint64_t oldPos = inStream->pos;
	inStream->pos = pos;
	U32 result = 0;
	int size = inStream->callback(&result, 4, inStream);
	inStream->pos = oldPos;
	*error = size != 4;
	return result;
}
static uint8_t LZ4e_Read8Stream(unsigned int pos, LZ4e_instream_t* inStream, int *error)
{
	uint64_t oldPos = inStream->pos;
	inStream->pos = pos;
	U32 result = 0;
	int size = inStream->callback(&result, 1, inStream);
	inStream->pos = oldPos;
	*error = size != 1;
	return result;
}
static int LZ4e_ReadStreamTemp(unsigned int pos, void *buf, unsigned int len, LZ4e_instream_t* inStream)
{
	uint64_t oldPos = inStream->pos;
	inStream->pos = pos;
	int size = inStream->callback(buf, len, inStream);
	inStream->pos = oldPos;
	return size;
}

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
LZ4_FORCE_O2_INLINE_GCC_PPC64LE
void LZ4_wildCopy(void* dstPtr, const void* srcPtr, void* dstEnd)
{
    BYTE* d = (BYTE*)dstPtr;
    const BYTE* s = (const BYTE*)srcPtr;
    BYTE* const e = (BYTE*)dstEnd;

    do { memcpy(d,s,8); d+=8; s+=8; } while (d<e);
}
static void LZ4_write16(void* memPtr, U16 value) { *(U16*)memPtr = value; }
static void LZ4_writeLE16(void* memPtr, U16 value)
{
    if (LZ4_isLittleEndian()) {
        LZ4_write16(memPtr, value);
    } else {
        BYTE* p = (BYTE*)memPtr;
        p[0] = (BYTE) value;
        p[1] = (BYTE)(value>>8);
    }
}
static void LZ4_write32(void* memPtr, U32 value)
{
    memcpy(memPtr, &value, sizeof(value));
}
#define STEPSIZE sizeof(reg_t)
//bufBase : pointer to buffer with length 2*bufLen
LZ4_FORCE_INLINE
unsigned LZ4e_count(U32 inOffs, U32 matchOffs, 
	BYTE* const bufBase, const U32 bufLen, 
	const BYTE* const inBufBase, const U32 inBufLen, const U32 inBufOffs,
	LZ4e_instream_t *pInStream)
{
	BYTE* const inBuf = bufBase;
	BYTE* const matchBuf = bufBase + bufLen;
	unsigned totalCount = 0;
	int inDataLen = 0;
	int matchDataLen = 0;
	const BYTE* pInLimit = NULL;
	const BYTE* pIn = NULL;
	const BYTE* pMatch = NULL;
	if (inOffs >= inBufOffs && inOffs + LASTLITERALS < inBufOffs + inBufLen)
	{
		pIn = inBufBase + (inOffs - inBufOffs);
		inDataLen = inBufLen - LASTLITERALS - (inOffs - inBufOffs);
		pInLimit = inBufBase + inBufLen - LASTLITERALS;
	}
	else
	{
		inDataLen = LZ4e_ReadStreamTemp(inOffs, inBuf, bufLen, pInStream);
		pIn = inBuf;
	}
	if (matchOffs >= inBufOffs && matchOffs < inBufOffs + inBufLen)
	{
		pMatch = inBufBase + (matchOffs - inBufOffs);
		matchDataLen = inBufLen - (matchOffs - inBufOffs);
	}
	else
	{
		matchDataLen = LZ4e_ReadStreamTemp(matchOffs, matchBuf, bufLen, pInStream);
		pMatch = matchBuf;
	}
	while (true)
	{
		const BYTE* const pInBase = pIn;

		if (unlikely(inDataLen <= LASTLITERALS || matchDataLen <= 0))
			break;
		if (unlikely(inDataLen - LASTLITERALS > matchDataLen))
			inDataLen = matchDataLen + LASTLITERALS;

		if (likely(pIn < pInLimit-(STEPSIZE-1))) {
			reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
			if (!diff) {
				pIn+=STEPSIZE; pMatch+=STEPSIZE;
			} else {
				return totalCount + LZ4_NbCommonBytes(diff);
		}   }

		while (likely(pIn < pInLimit-(STEPSIZE-1))) {
			reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
			if (!diff) { pIn+=STEPSIZE; pMatch+=STEPSIZE; continue; }
			return totalCount + (pIn-pInBase) + LZ4_NbCommonBytes(diff);
		}

		if ((STEPSIZE==8) && (pIn<(pInLimit-3)) && (LZ4_read32(pMatch) == LZ4_read32(pIn))) {pIn+=4; pMatch+=4;}
		if ((pIn<(pInLimit-1)) && (LZ4_read16(pMatch) == LZ4_read16(pIn))) {pIn+=2; pMatch+=2;}
		if ((pIn<pInLimit) && (*pMatch == *pIn)) pIn++;

		if ((pIn-pInBase) == 0)
			break;

		totalCount += pIn-pInBase;
		inOffs += pIn-pInBase;
		matchOffs += pIn-pInBase;

		inDataLen = LZ4e_ReadStreamTemp(inOffs, inBuf, bufLen, pInStream);
		matchDataLen = LZ4e_ReadStreamTemp(matchOffs, matchBuf, bufLen, pInStream);
		//if (unlikely(inDataLen < bufLen || matchDataLen < bufLen))
		//	break;

		pInLimit = inBuf + inDataLen - LASTLITERALS;
		pIn = inBuf;
		pMatch = matchBuf;
	}
    return totalCount;
}

LZ4_FORCE_O2_GCC_PPC64LE
LZ4_FORCE_INLINE unsigned int LZ4_compress_generic(
                 LZ4_stream_t_internal* const cctx,
                 LZ4e_instream_t* const ssource,
                 LZ4e_outstream_t* const sdest,
				 void* const streamBuf,
				 unsigned int streamBufSize,
                 const tableType_t tableType,
                 const U32 acceleration)
{
	unsigned int totalInputSize = 0;
	unsigned int totalOutputSize = 0;
    BYTE* ipb = (BYTE*)streamBuf;

    BYTE* const baseb = (BYTE*)streamBuf;
    const BYTE* lowLimitb;

    const LZ4_stream_t_internal* dictCtx = (const LZ4_stream_t_internal*) cctx->dictCtx;
    const BYTE* const dictionary = cctx->dictionary;
    const U32 dictSize = cctx->dictSize;
    const U32 dictDelta = 0;   /* make indexes in dictCtx comparable with index in current context */

    const BYTE* const dictEnd = dictionary + dictSize;
	unsigned int anchor = 0;
    BYTE* const iendb = ipb + streamBufSize;

    /* the dictCtx currentOffset is indexed on the start of the dictionary,
     * while a dictionary in the current context precedes the currentOffset */
    const BYTE* dictBase = dictionary + dictSize;

	BYTE* const opbaseb = (BYTE*)streamBuf + streamBufSize * 2;
    BYTE* opb = (BYTE*)streamBuf + streamBufSize * 2;
    BYTE* const olimitb = (BYTE*)streamBuf + streamBufSize * 3;

	BYTE* const tempbase = (BYTE*)streamBuf + streamBufSize;
	BYTE* const templimit = (BYTE*)streamBuf + streamBufSize * 2;
	U32 outputOffset = 0;

    U32 offset = 0;
    U32 forwardH;

    DEBUGLOG(5, "LZ4_compress_generic: streamBufSize=%u, tableType=%u", streamBufSize, tableType);
	if (streamBufSize < LZ4_64Klimit) return 0;
    /* Init conditions */
    //if (outputLimited == fillOutput && maxOutputSize < 1) return 0; /* Impossible to store anything */
    //if ((U32)inputSize > (U32)LZ4_MAX_INPUT_SIZE) return 0;   /* Unsupported inputSize, too large (or negative) */
    //if ((tableType == byU16) && (inputSize>=LZ4_64Klimit)) return 0;  /* Size too large (not within 64K limit) */
    if (tableType==byPtr) return 0;//assert(dictDirective==noDict);      /* only supported use case with byPtr */
    assert(acceleration >= 1);

    lowLimitb = (const BYTE*)streamBuf;

	int curSize = LZ4e_inputBuffer(baseb, ipb, iendb, ssource);
	unsigned int curOffset = 0;
    if (curSize<LZ4_minLength) goto _last_literals;        /* Input too small, no compression (all literals) */


    /* First Byte */
    LZ4_putPosition(ipb, curOffset, cctx->hashTable, tableType);
    ipb++; curOffset++; forwardH = LZ4_hashPosition(ipb, tableType);

    /* Main Loop */
    for ( ; ; ) {
        BYTE* token;
		U32 matchIndex;

        /* Find a match */
        {   /* byU32, byU16 */

            BYTE* forwardIp = ipb;
            unsigned step = 1;
            unsigned searchMatchNb = acceleration << LZ4_skipTrigger;
            do {
                U32 const h = forwardH;
                U32 const current = curOffset + (forwardIp - ipb);//(U32)(forwardIp - base);
                matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);
                assert(matchIndex <= current);
				assert(current < (ptrdiff_t)(2 GB - 1));
                //assert(forwardIp - base < (ptrdiff_t)(2 GB - 1));
				curOffset += (forwardIp - ipb);
				assert(forwardIp >= ipb);
                ipb = forwardIp;
                forwardIp += step;
                step = (searchMatchNb++ >> LZ4_skipTrigger);

				if (unlikely(forwardIp-baseb >= (curSize - MFLIMIT + 1)))
				{
					if (likely(curSize == streamBufSize))
					{
						curSize = LZ4e_inputBuffer(baseb, ipb, iendb, ssource);
						forwardIp = ipb + step;
						if (unlikely(forwardIp-baseb + MFLIMIT >= (curSize + 1)))
							goto _last_literals; //still not enough read
					}
					else
						goto _last_literals; //nothing left to read
				}
                //if (unlikely(forwardIp > mflimitPlusOne)) goto _last_literals;
                //assert(ip < mflimitPlusOne);
				assert(ipb-baseb < (curSize - MFLIMIT + 1));

                forwardH = LZ4_hashPosition(forwardIp, tableType);
                LZ4_putIndexOnHash(current, h, cctx->hashTable, tableType);

                assert(matchIndex < current);
                if ((tableType != byU16) && (matchIndex+MAX_DISTANCE < current)) continue;  /* too far */
                if (tableType == byU16) assert((current - matchIndex) <= MAX_DISTANCE);     /* too_far presumed impossible with byU16 */
				
				int error;
				if (likely((curOffset - matchIndex) <= (ipb-baseb)))
				{
					if (LZ4_read32(ipb - (curOffset - matchIndex)) == LZ4_read32(ipb)) {
						offset = current - matchIndex;
						break; //match index is inside the read buffer
					}
				}
				else if (LZ4e_Read32Stream(matchIndex, ssource, &error) == LZ4_read32(ipb) && likely(!error)) {
					offset = current - matchIndex;
                    //if (maybe_extMem) offset = current - matchIndex;
                    break;   /* match found */
                }
				else if (unlikely(error))
				{
					DEBUGLOG(1, "break 15");
					return 0;
				}
            } while(1);
        }

        /* Catch up */
		{
			unsigned int startOffset = curOffset - (ipb - baseb);
			unsigned int tempStartOffset = 0, tempLen = 0;
			while ((curOffset>anchor) & (matchIndex > 0))
			{
				BYTE match, ip;
				if (matchIndex <= startOffset)
				{
					if (unlikely(tempLen == 0 || matchIndex <= tempStartOffset))
					{
						tempStartOffset = (matchIndex < streamBufSize) ? 0 : (matchIndex - streamBufSize);
						tempLen = LZ4e_ReadStreamTemp(tempStartOffset, tempbase, streamBufSize, ssource);
						if (unlikely((tempStartOffset + tempLen) < matchIndex))
						{
							DEBUGLOG(1, "break 14");
							return 0;
						}
					}
					match = tempbase[matchIndex - tempStartOffset - 1];
				}
				else
					match = baseb[matchIndex - startOffset - 1];
				if (unlikely(ipb == baseb))
				{
					int error;
					ip = LZ4e_Read8Stream(startOffset - 1, ssource, &error);
					if (unlikely(error))
					{
						DEBUGLOG(1, "break 13");
						return 0;
					}
					if (unlikely(match == ip))
					{
						unsigned int offset = (curOffset < 16) ? curOffset : 16;
						unsigned int movesize = streamBufSize - offset;
						if (movesize > curSize)
							movesize = curSize;
						memmove(&baseb[offset], baseb, movesize);

						ssource->pos = startOffset - offset;
						if (unlikely(ssource->callback(baseb, offset, ssource) != offset))
						{
							DEBUGLOG(1, "break 12");
							return 0;
						}

						ipb += offset;
						curSize = movesize + offset;
						startOffset -= offset;
					}
				}
				else
					ip = ipb[-1];
				if (unlikely(match == ip))
				{
					ipb--; curOffset--;
					matchIndex--;
				}
				else
					break;
			}
		}
        
		/*if (likely((curOffset - matchIndex) <= (ipb-baseb)))
		{
			DEBUGLOG(6, "match for %x (0x%x) found at %x (0x%x), anchor %x", curOffset, LZ4_read32(ipb), matchIndex, LZ4_read32(ipb - (curOffset - matchIndex)), anchor);
		}
		else
		{
			int error;
			DEBUGLOG(6, "match for %x (0x%x) found at %x (0x%x), anchor %x", curOffset, LZ4_read32(ipb), matchIndex, LZ4e_Read32Stream(matchIndex, ssource, &error), anchor);
		}*/
        //while (((curOffset>anchor) & (matchIndex > 0)) && (unlikely(ip[-1]==match[-1]))) { ip--; match--; }
		unsigned litLength = (unsigned)(curOffset - anchor);//(ip - anchor);
        /* Encode Literals */
        {   
			if (unlikely((2 + 1 + LASTLITERALS) + (litLength/255) + 2 > streamBufSize - (opb - opbaseb)))
			{
				if (unlikely((2 + 1 + LASTLITERALS) + (litLength/255) > streamBufSize))
				{
					DEBUGLOG(1, "break 11");
					return 0;
				}
				unsigned const expectedLen = opb-opbaseb;
				if (expectedLen != LZ4e_outputBuffer(opbaseb, opb, olimitb, sdest))
				{
					DEBUGLOG(1, "break 10");
					return 0;
				}
				totalOutputSize += expectedLen;
			}
            token = opb++;
            if (litLength >= RUN_MASK) {
                int len = (int)litLength-RUN_MASK;
                *token = (RUN_MASK<<ML_BITS);
                for(; len >= 255 ; len-=255) *opb++ = 255;
                *opb++ = (BYTE)len;
            }
            else *token = (BYTE)(litLength<<ML_BITS);
			//Copy the literals after the match length has been determined, 
			//since the token at the beginning of the block has to be updated for the match length.written before the literals has to be still accessible at that point.
            DEBUGLOG(6, "seq.start:%x, literals=%x, match.start:%x",
                        (int)(anchor), litLength, (int)(curOffset));
        }

_next_match:
        /* at this stage, the following variables must be correctly set :
         * - ip : at start of LZ operation
         * - match : at start of previous pattern occurence; can be within current prefix, or within extDict
         * - offset : if maybe_ext_memSegment==1 (constant)
         * - lowLimit : must be == dictionary to mean "match is within extDict"; must be == source otherwise
         * - token and *token : position to write 4-bits for match length; higher 4-bits for literal length supposed already written
         */


        /* Encode MatchLength */
		unsigned matchCode;
        {  
			matchCode = LZ4e_count(curOffset+MINMATCH, matchIndex+MINMATCH, tempbase, streamBufSize/2, baseb, curSize, curOffset - (ipb-baseb), ssource);
			//The token needs to be updated before flushing the output buffer (which contains the token field).
			if (matchCode >= ML_MASK)
				*token += ML_MASK;
			else
				*token += (BYTE)(matchCode);
		}
		/* Write Literals (if any) */
		{
			U32 readOffs = 0;
			while (litLength > 0)
			{
				BYTE* pAnchor; U32 readLength;
				if (unlikely(anchor+readOffs < (curOffset - (ipb-baseb)) || anchor+readOffs+litLength+8 > curOffset+(baseb+curSize-ipb)))
				{
					if (litLength < streamBufSize) readLength = litLength;
					else readLength = streamBufSize;
					if ((readLength = LZ4e_ReadStreamTemp(anchor+readOffs, tempbase, readLength, ssource)) <= 0)
					{
						DEBUGLOG(1, "break 9");
						return 0;
					}
					if (readLength > litLength) readLength = litLength;
					pAnchor = tempbase;
				}
				else
				{
					pAnchor = &baseb[anchor+readOffs - (curOffset - (ipb-baseb))];
					readLength = (baseb + curSize) - pAnchor;
					if (readLength > litLength) readLength = litLength;
				}
				if (readLength+8 > streamBufSize - (opb-opbaseb))
				{
					unsigned const expectedLen = opb-opbaseb;
					if (expectedLen != LZ4e_outputBuffer(opbaseb, opb, olimitb, sdest))
					{
						DEBUGLOG(1, "break 8");
						return 0;
					}
					totalOutputSize += expectedLen;
				}
				U32 outputCount = readLength;
				if (readLength > streamBufSize - (opb-opbaseb) - 8)
					outputCount = streamBufSize - (opb-opbaseb) - 8;
				/* Copy Literals */
				LZ4_wildCopy(opb, pAnchor, opb+outputCount);
				opb+=outputCount;
				litLength-=outputCount;
				readOffs+=outputCount;
			}
		}
        LZ4_writeLE16(opb, (U16)offset); opb+=2;
		{
            //matchCode = LZ4_count(ip+MINMATCH, match+MINMATCH, matchlimit);
            ipb += MINMATCH + matchCode;
			curOffset += MINMATCH + matchCode;
            DEBUGLOG(6, "             with matchLength=%x and match.source=%x", matchCode+MINMATCH, matchIndex);
			if (unlikely((1 + LASTLITERALS) + (matchCode>>8) > streamBufSize-(opb-opbaseb)))
			{
				if (unlikely((1 + LASTLITERALS) + (matchCode>>8) > streamBufSize))
				{
					DEBUGLOG(1, "break 7");
					return 0;
				}
				unsigned const expectedLen = opb-opbaseb;
				if (expectedLen != LZ4e_outputBuffer(opbaseb, opb, olimitb, sdest))
				{
					DEBUGLOG(1, "break 6");
					return 0;
				}
				totalOutputSize += expectedLen;
			}
            if (matchCode >= ML_MASK) {
                //*token += ML_MASK;
                matchCode -= ML_MASK;
                LZ4_write32(opb, 0xFFFFFFFF);
                while (matchCode >= 4*255) {
                    opb+=4;
                    LZ4_write32(opb, 0xFFFFFFFF);
                    matchCode -= 4*255;
                }
                opb += matchCode / 255;
                *opb++ = (BYTE)(matchCode % 255);
            } //else
            //    *token += (BYTE)(matchCode);
        }

        //anchor = ip;
		anchor = curOffset;

        /* Test end of chunk */
		if (ipb >= baseb + curSize - MFLIMIT + 1)
		{
			if (curSize < streamBufSize) break;
			curSize = LZ4e_inputBuffer(baseb, ipb, iendb, ssource);
			if (ipb >= baseb + curSize - MFLIMIT + 1) break;
		}
        //if (ip >= mflimitPlusOne) break;

        /* Fill table */
		//Since LZ4e_inputBuffer keeps previous data (up to 64K), there should always be 2 bytes behind ipb at this place.
		LZ4_putPosition(ipb-2, curOffset-2, cctx->hashTable, tableType); 
        //LZ4_putPosition(ip-2, cctx->hashTable, tableType, base);

        /* Test next position */
		{   /* byU32, byU16 */
			U32 const h = LZ4_hashPosition(ipb, tableType);
            U32 const current = curOffset;//(U32)(ip-base);
            matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);
            assert(matchIndex < current);

            LZ4_putIndexOnHash(current, h, cctx->hashTable, tableType);
            assert(matchIndex < current);
            if ( ((tableType==byU16) ? 1 : (matchIndex+MAX_DISTANCE >= current)) )
			{
				int error = 0;
				if (likely((curOffset - matchIndex) <= (ipb-baseb)) 
						? (LZ4_read32(ipb - (curOffset - matchIndex)) == LZ4_read32(ipb))
						: (LZ4e_Read32Stream(matchIndex, ssource, &error) == LZ4_read32(ipb))) {
					if (unlikely(error))
					{
						DEBUGLOG(1, "break 5");
						return 0;
					}
					token=opb++;
					*token=0;
					//if (maybe_extMem) offset = current - matchIndex;
					offset = current - matchIndex;
					DEBUGLOG(6, "seq.start:%x, literals=%u, match.start:%x",
								(int)(anchor), 0, (int)(curOffset));
					goto _next_match;
				}
            }
        }

        /* Prepare next loop */
        forwardH = LZ4_hashPosition(++ipb, tableType);
		++curOffset;
    }

_last_literals:
    /* Encode Last Literals */
	{ 
		size_t lastRun = curOffset + (baseb+curSize-ipb) - anchor;
		int curSizeTemp = curSize; unsigned int curTempOffset = curOffset + (baseb+curSize-ipb);
		while (curSizeTemp == streamBufSize)
		{
			curSizeTemp = LZ4e_ReadStreamTemp(curTempOffset, tempbase, streamBufSize, ssource);
			//if (curSizeTemp >= 0)
			{
				lastRun += (size_t)curSizeTemp;
				curTempOffset += (unsigned int)curSizeTemp;
			}
		}
		unsigned const expectedLen = opb-opbaseb;
		if (expectedLen != LZ4e_outputBuffer(opbaseb, opb, olimitb, sdest))
		{
			DEBUGLOG(1, "break 4");
			return 0;
		}
		totalOutputSize += expectedLen;
		if (1 + ((lastRun+255-RUN_MASK)/255) > streamBufSize)
		{
			DEBUGLOG(1, "break 3");
			return 0;
		}
        if (lastRun >= RUN_MASK) {
            size_t accumulator = lastRun - RUN_MASK;
            *opb++ = RUN_MASK << ML_BITS;
            for(; accumulator >= 255 ; accumulator-=255) *opb++ = 255;
            *opb++ = (BYTE) accumulator;
        } else {
            *opb++ = (BYTE)(lastRun<<ML_BITS);
        }
		curTempOffset = anchor;
		while (lastRun > 0) {
			unsigned int batchSize = streamBufSize;
			if (batchSize > lastRun)
				batchSize = (unsigned int)lastRun;
			curSizeTemp = LZ4e_ReadStreamTemp(curTempOffset, tempbase, batchSize, ssource);
			if (curSizeTemp != batchSize)
			{
				DEBUGLOG(1, "break 2");
				return 0;
			}
			unsigned const expectedLen = opb-opbaseb;
			if (expectedLen != LZ4e_outputBuffer(opbaseb, opb, olimitb, sdest))
			{
				DEBUGLOG(1, "break 1");
				return 0;
			}
			totalOutputSize += expectedLen;
			memcpy(opb, tempbase, batchSize);
			curTempOffset += batchSize;
			opb += batchSize;
			lastRun -= batchSize;
		}
		ssource->pos = curTempOffset;
	}
	
    cctx->dictSize += (U32)totalInputSize;
    cctx->currentOffset += (U32)totalInputSize;
	unsigned const expectedLen = opb-opbaseb;
	if (expectedLen != LZ4e_outputBuffer(opbaseb, opb, olimitb, sdest))
	{
		DEBUGLOG(1, "break 0");
		return 0;
	}
	totalOutputSize += expectedLen;
    DEBUGLOG(5, "LZ4_compress_generic: compressed %llu bytes into %i bytes", ssource->pos, totalOutputSize);
    return totalOutputSize;//(int)(((char*)op) - dest);
}

unsigned int LZ4_compress_fast_extState(void* state, LZ4e_instream_t* source, LZ4e_outstream_t* dest, void *streamBuf, int acceleration, unsigned int totalSize, unsigned int streamBufSize)
{
    LZ4_stream_t_internal* ctx = &((LZ4_stream_t*)state)->internal_donotuse;
    if (acceleration < 1) acceleration = ACCELERATION_DEFAULT;
    LZ4_resetStream((LZ4_stream_t*)state);
    if (totalSize < LZ4_64Klimit) {
        return LZ4_compress_generic(ctx, source, dest, streamBuf, streamBufSize, byU16, acceleration);
    } else {
        return LZ4_compress_generic(ctx, source, dest, streamBuf, streamBufSize, byU32, acceleration);
    }
}

unsigned int LZ4e_compress_fast(LZ4e_instream_t* source, LZ4e_outstream_t* dest, int acceleration, unsigned int totalSize, unsigned int streamBufSize)
{
    unsigned int result;
#if (LZ4_HEAPMODE)
    LZ4_stream_t* ctxPtr = (LZ4_stream_t*)ALLOC(sizeof(LZ4_stream_t));   /* malloc-calloc always properly aligned */
    if (ctxPtr == NULL) return 0;
	if (streamBufSize < LZ4_64Klimit)
		streamBufSize = LZ4_64Klimit;
	void* streamBuf = ALLOC(streamBufSize * 3);
	if (streamBuf == NULL)
	{
		FREEMEM(ctxPtr);
		return 0;
	}
#else
    LZ4_stream_t ctx;
    LZ4_stream_t* const ctxPtr = &ctx;
	streamBufSize = LZ4_64Klimit;
	BYTE streamBuf[LZ4_64Klimit * 3];
#endif
    result = LZ4_compress_fast_extState(ctxPtr, source, dest, streamBuf, acceleration, totalSize, streamBufSize);

#if (LZ4_HEAPMODE)
    FREEMEM(streamBuf);
    FREEMEM(ctxPtr);
#endif
    return result;
}