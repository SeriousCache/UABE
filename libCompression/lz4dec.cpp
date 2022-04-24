#pragma once
#include "lz4dec.h"
#include <stddef.h>
#include <string.h>

#ifdef _MSC_VER    /* Visual Studio */
#  include <intrin.h>
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4293)        /* disable: C4293: too large shift (32-bits) */
#endif  /* _MSC_VER */

#ifndef FORCE_INLINE
#  ifdef _MSC_VER    /* Visual Studio */
#    define FORCE_INLINE static __forceinline
#  else
#    if defined (__cplusplus) || defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#      ifdef __GNUC__
#        define FORCE_INLINE static inline __attribute__((always_inline))
#      else
#        define FORCE_INLINE static inline
#      endif
#    else
#      define FORCE_INLINE static
#    endif /* __STDC_VERSION__ */
#  endif  /* _MSC_VER */
#endif /* FORCE_INLINE */

//Branch optimization stuff (expected scenario == true for likely, false for unlikely).
#if (defined(__GNUC__) && (__GNUC__ >= 3)) || (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 800)) || defined(__clang__)
#  define expect(expr,value)    (__builtin_expect ((expr),(value)) )
#else
#  define expect(expr,value)    (expr)
#endif

#define likely(expr)     expect((expr) != 0, 1)
#define unlikely(expr)   expect((expr) != 0, 0)


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

/*-************************************
*  Local Structures and types
**************************************/
typedef enum { notLimited = 0, limitedOutput = 1 } limitedOutput_directive;
typedef enum { byPtr, byU32, byU16 } tableType_t;

typedef enum { noDict = 0, withPrefix64k, usingExtDict } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { full = 0, partial = 1 } earlyEnd_directive;

/*******************************
*  Decompression functions
*******************************/
static U16 LZ4_read16(const void* memPtr) { return *(const U16*) memPtr; }

static unsigned LZ4_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental */
    return one.c[0];
}

static void LZ4_write32(void* memPtr, U32 value)
{
    memcpy(memPtr, &value, sizeof(value));
}

static U16 LZ4_readLE16(const void* memPtr)
{
    if (LZ4_isLittleEndian()) {
        return LZ4_read16(memPtr);
    } else {
        const BYTE* p = (const BYTE*)memPtr;
        return (U16)((U16)p[0] + (p[1]<<8));
    }
}
static void LZ4_copy8(void* dst, const void* src)
{
    memcpy(dst,src,8);
}
/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
static void LZ4_wildCopy(void* dstPtr, const void* srcPtr, void* dstEnd)
{
    BYTE* d = (BYTE*)dstPtr;
    const BYTE* s = (const BYTE*)srcPtr;
    BYTE* const e = (BYTE*)dstEnd;

    do { LZ4_copy8(d,s); d+=8; s+=8; } while (d<e);
}

//Returns the bytes read.
static int LZ4e_inputBuffer(BYTE* istart, BYTE* ip, BYTE* imax, LZ4e_instream_t* inStream)
{
	inStream->pos += (ip-istart);
	return inStream->callback(istart, imax-istart, inStream);
}
//Flushes all written data and keeps the last written 64KiB (dictionary) at ostart.
//Returns the new dictionary size (max. 64KiB), or returns omax - ostart on error.
static int LZ4e_outputBuffer(BYTE* ostart, BYTE* op, BYTE* omax, BYTE* odend, LZ4e_outstream_t* outStream)
{
	if (op < odend)
		return omax - ostart;
	if (outStream->callback(odend, op-odend, outStream) < op-odend)
		return omax - ostart;

	int newDictionarySize = 0;
	int curDataSize = op - ostart;
	if (curDataSize > 64 KB)
	{
		memmove(ostart, op - (64 KB), 64 KB);
		newDictionarySize = 64 KB;
	}
	else
	{
		newDictionarySize = curDataSize;
	}
	return newDictionarySize;
}

static int LZ4_decompress_generic(
                 char* const sourceBuf,
                 char* const destBuf,
                 int inputBufSize,
                 int outputBufSize,         /* If endOnInput==endOnInputSize, this value is the max size of Output Buffer. */

                 int endOnInput,         /* endOnOutputSize, endOnInputSize */

				 LZ4e_instream_t* inStream,
				 LZ4e_outstream_t* outStream
                 )
{
    /* Local Variables */
    BYTE* const istart = (BYTE*) sourceBuf;
    BYTE* ip = istart;
    BYTE* iend = istart + 0;
    BYTE* const imax = istart + inputBufSize - 8;
	
    BYTE* const ostart = (BYTE*) destBuf;
    BYTE* op = ostart;
	BYTE* odend = ostart + 0; //dictionary end
    BYTE* const omax = ostart + outputBufSize - 8;

    BYTE* cpy;
    const BYTE* const lowLimit = ostart;

    const unsigned dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};
    const int dec64table[] = {0, 0, 0, -1, 0, 1, 2, 3};

    const int safeDecode = (endOnInput==endOnInputSize);
    const int checkOffset = (safeDecode);

	if (outputBufSize < (28 + 64 KB)) return -1;
	if (inputBufSize < 16) return -1;

    /* Main Loop : decode sequences */
    while (1) {
        size_t length;
        const BYTE* match;
        size_t offset;

		if (unlikely(iend-ip==0))
		{
			iend = istart + LZ4e_inputBuffer(istart, ip, imax, inStream);
			ip = istart;
			if (unlikely(iend-ip==0)) {
				goto _output_error;  /* overflow detection */
			}
		}
        /* get literal length */
        unsigned const token = *ip++;
        if ((length=(token>>ML_BITS)) == RUN_MASK) {
            unsigned s;
            do {
				if (unlikely(ip+RUN_MASK+1>=iend)) {
					iend = istart + LZ4e_inputBuffer(istart, ip, imax, inStream);
					ip = istart;
					if (unlikely(iend==istart)) {
						goto _output_error;
					}
				}
                s = *ip++;
                length += s;
            } while ( likely(endOnInput ? ip<iend-RUN_MASK : 1) & (s==255) );
            if ((safeDecode) && unlikely((uptrval)(op)+length<(uptrval)(op))) {
				goto _output_error;   /* overflow detection */
			}
            if ((safeDecode) && unlikely((uptrval)(ip)+length<(uptrval)(ip))) {
				goto _output_error;   /* overflow detection */
			}
        }

        /* copy literals */
		while (length)
		{
			size_t ci = (ip+length>iend)?(iend-ip):(length);
			size_t c;
			if (!ci) {
				iend = istart + LZ4e_inputBuffer(istart, ip, imax, inStream);
				ip = istart;
				if (unlikely(iend==istart)) {
					goto _output_error;
				}
				continue;
			}
			c = (op+ci>omax)?(omax-op):(ci);
			if (!c) {
				odend = ostart + LZ4e_outputBuffer(ostart, op, omax, odend, outStream);
				op = odend;
				if (unlikely(odend==omax)) {
					goto _output_error;
				}
				continue;
			}
			LZ4_wildCopy(op, ip, op+c);
			ip += c; op += c;
			length -= c;
		}

		//Compressed data may end here.
		if (unlikely(ip+2>iend)) {
			iend = istart + LZ4e_inputBuffer(istart, ip, imax, inStream);
			ip = istart;
			if ((iend-ip)<2) //(unlikely((iend-ip)<3))
			{
				//Only throw an error if there still are bytes to copy.
				if ((token & ML_MASK) != 0) {
					goto _output_error;
				}
				break;
			}
		}
		if (unlikely(op+20>omax)) {
			odend = ostart + LZ4e_outputBuffer(ostart, op, omax, odend, outStream);
			op = odend;
			if (unlikely(odend==omax)) {
				goto _output_error;
			}
		}

        /* get offset */
        offset = LZ4_readLE16(ip); ip+=2;
        match = op - offset;
        if ((checkOffset) && (unlikely(match < lowLimit))) {
			goto _output_error;   /* Error : offset outside buffers */
		}
        LZ4_write32(op, (U32)offset);   /* costs ~1%; silence an msan warning when offset==0 */

        /* get matchlength */
        length = token & ML_MASK;
        if (length == ML_MASK) {
            unsigned s;
            do {
				if (unlikely(ip+LASTLITERALS>iend)) {
					iend = istart + LZ4e_inputBuffer(istart, ip, imax, inStream);
					ip = istart;
					if (unlikely(ip+LASTLITERALS>iend)) {
						goto _output_error;
					}
					//if ((endOnInput) && (ip > iend-LASTLITERALS)) goto _output_error;
				}
                s = *ip++;
                length += s;
            } while (s==255);
            if ((safeDecode) && unlikely((uptrval)(op)+length<(uptrval)op)) {
				goto _output_error;   /* overflow detection */
			}
        }
        length += MINMATCH;
		//As the dictionary boundaries shift with each byte written, the values copied from match can be beyond the current output pointer.
		//This is no issue since it can never exceed the output buffer location that also moves on in the same 'speed' as the match location while copying.

        /* copy match within block */
        cpy = op + length;
        if (unlikely(offset<8)) {
            const int dec64 = dec64table[offset];
            op[0] = match[0];
            op[1] = match[1];
            op[2] = match[2];
            op[3] = match[3];
            match += dec32table[offset];
            memcpy(op+4, match, 4);
            match -= dec64;
        } else { LZ4_copy8(op, match); match+=8; }
        op += 8;

        {
            LZ4_copy8(op, match); 
			op += 8; match += 8; 
			if (length > 16)
			{
				length -= 16;
				while (length)
				{
					size_t c = (op+length>omax)?(omax-op):(length);
					if (!c) {
						size_t oldOffs = cpy - op;
						odend = ostart + LZ4e_outputBuffer(ostart, op, omax, odend, outStream);
						if (unlikely(odend==omax)) {
							goto _output_error;
						}
						match -= op-odend;
						op = odend;
						cpy = op + oldOffs;
						continue;
					}
					LZ4_wildCopy(op, match, op+c);
					op += c; match += c;
					length -= c;
				}
			}
        }
        op=cpy;   /* correction */


    }
	
    /* end of decoding */
    //if (endOnInput)
    //   return (int) (((char*)op)-dest);     /* Nb of output bytes decoded */
    //else
    //   return (int) (((const char*)ip)-source);   /* Nb of input bytes read */
	inStream->pos += (ip - istart);
	return LZ4e_outputBuffer(ostart, op, omax, odend, outStream) != (omax-ostart);
    /* Overflow error detected */
_output_error:
    return (int) (-(((const char*)ip)-sourceBuf))-1;
}


int LZ4e_decompress_safe(char* sourceBuf, char* destBuf, int sourceBufSize, int destBufSize, LZ4e_instream_t* inStream, LZ4e_outstream_t* outStream)
{
    return LZ4_decompress_generic(sourceBuf, destBuf, sourceBufSize, destBufSize, endOnInputSize, inStream, outStream);
}