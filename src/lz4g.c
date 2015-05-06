/*
  LZ4iog.c - LZ4 FILE* API Interface
  Copyright (C) Yann Collet 2011-2015
 
BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  You can contact the author at :
  - LZ4 source repository : https://github.com/Cyan4973/lz4
  - LZ4 public forum : https://groups.google.com/forum/#!forum/lz4c
*/

/**************************************
*  Compiler Options
**************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_DEPRECATE     /* VS2005 */
#  pragma warning(disable : 4127)      /* disable: C4127: conditional expression is constant */
#endif

#define _LARGE_FILES           /* Large file support on 32-bits AIX */
#define _FILE_OFFSET_BITS 64   /* Large file support on 32-bits unix */


/*****************************
*  Includes
*****************************/
#include <stdio.h>     /* fprintf, fopen, fread, stdin, stdout */
#include <stdlib.h>    /* malloc, free */
#include <string.h>    /* strcmp, strlen */
#include <time.h>      /* clock */
#include <sys/types.h> /* stat64 */
#include <sys/stat.h>  /* stat64 */
#include "lz4.h"      /* still required for legacy format */
#include "lz4hc.h"    /* still required for legacy format */
#include "lz4frame.h"

/******************************
*  OS-specific Includes
******************************/
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>   /* _O_BINARY */
#  include <io.h>      /* _setmode, _fileno, _get_osfhandle */
#  define SET_BINARY_MODE(file) _setmode(_fileno(file), _O_BINARY)
#  include <Windows.h> /* DeviceIoControl, HANDLE, FSCTL_SET_SPARSE */
#  define SET_SPARSE_FILE_MODE(file) { DWORD dw; DeviceIoControl((HANDLE) _get_osfhandle(_fileno(file)), FSCTL_SET_SPARSE, 0, 0, 0, 0, &dw, 0); }
#  if defined(_MSC_VER) && (_MSC_VER >= 1400)  /* Avoid MSVC fseek()'s 2GiB barrier */
#    define fseek _fseeki64
#  endif
#else
#  define SET_BINARY_MODE(file)
#  define SET_SPARSE_FILE_MODE(file)
#endif


/*****************************
*  Constants
*****************************/
#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define _1BIT  0x01
#define _2BITS 0x03
#define _3BITS 0x07
#define _4BITS 0x0F
#define _8BITS 0xFF

#define MAGICNUMBER_SIZE    4
#define LZ4G_MAGICNUMBER   0x184D2204
#define LZ4G_SKIPPABLE0    0x184D2A50
#define LZ4G_SKIPPABLEMASK 0xFFFFFFF0
#define LEGACY_MAGICNUMBER  0x184C2102

#define CACHELINE 64
#define LEGACY_BLOCKSIZE   (8 MB)
#define MIN_STREAM_BUFSIZE (192 KB)
#define LZ4G_BLOCKSIZEID_DEFAULT 7

#define sizeT sizeof(size_t)
#define maskT (sizeT - 1)



/**************************
 * macro for error handling that sets *errstring and returns number of bytes written.
 ************************/

#define LZ4G_RETURN_ERROR_DOTS(errnumber, fmt, ...)       \
{ \
  return snprintf(*errstring, *nerrbytes, "%s:%i error %i: "#fmt, __FILE__, __LINE__, errnumber,__VA_ARGS__); \
}

#define LZ4G_RETURN_ERROR(errnumber, fmt)       \
{ \
  return snprintf(*errstring, *nerrbytes, "%s:%i error %i: "#fmt, __FILE__, __LINE__, errnumber); \
}


#define LZ4G_BLOCKSIZEID_DEFAULT 7

/**************************************
*  Local Parameters
**************************************/
static int g_overwrite = 1;
static int g_blockSizeId = LZ4G_BLOCKSIZEID_DEFAULT;
static int g_blockChecksum = 0;
static int g_streamChecksum = 1;
static int g_blockIndependence = 1;
static int g_sparseFileSupport = 0;
static int g_contentSizeFlag = 0;

static const int minBlockSizeID = 4;
static const int maxBlockSizeID = 7;

/**************************************
*  Version modifiers
**************************************/
#define EXTENDED_ARGUMENTS
#define EXTENDED_HELP
#define EXTENDED_FORMAT
#define LZ4G_DEFAULT_DECOMPRESSOR LZ4G_decodeLZ4S



/* ************************************************** */
/* ****************** Parameters ******************** */
/* ************************************************** */

/* Default setting : overwrite = 1; return : overwrite mode (0/1) */
int LZ4G_setOverwrite(int yes)
{
   g_overwrite = (yes!=0);
   return g_overwrite;
}

/* blockSizeID : valid values : 4-5-6-7 */
int LZ4G_setBlockSizeID(int bsid)
{
    static const int blockSizeTable[] = { 64 KB, 256 KB, 1 MB, 4 MB };
    if ((bsid < minBlockSizeID) || (bsid > maxBlockSizeID)) return -1;
    g_blockSizeId = bsid;
    return blockSizeTable[g_blockSizeId-minBlockSizeID];
}

typedef enum { LZ4G_blockLinked=0, LZ4G_blockIndependent} LZ4G_blockMode_t;
int LZ4G_setBlockMode(LZ4G_blockMode_t blockMode)
{
    g_blockIndependence = (blockMode == LZ4G_blockIndependent);
    return g_blockIndependence;
}

/* Default setting : no checksum */
int LZ4G_setBlockChecksumMode(int xxhash)
{
    g_blockChecksum = (xxhash != 0);
    return g_blockChecksum;
}

/* Default setting : checksum enabled */
int LZ4G_setStreamChecksumMode(int xxhash)
{
    g_streamChecksum = (xxhash != 0);
    return g_streamChecksum;
}


/* Default setting : 0 (disabled) */
int LZ4G_setSparseFile(int enable)
{
    g_sparseFileSupport = (enable!=0);
    return g_sparseFileSupport;
}

/* Default setting : 0 (disabled) */
int LZ4G_setContentSize(int enable)
{
    g_contentSizeFlag = (enable!=0);
    return g_contentSizeFlag;
}


static int LZ4G_GetBlockSize_FromBlockId (int id) { return (1 << (8 + (2 * id))); }
static int LZ4G_isSkippableMagicNumber(unsigned int magic) { return (magic & LZ4G_SKIPPABLEMASK) == LZ4G_SKIPPABLE0; }

static unsigned LZ4G_readLE32 (const void* s)
{
    const unsigned char* srcPtr = (const unsigned char*)s;
    unsigned value32 = srcPtr[0];
    value32 += (srcPtr[1]<<8);
    value32 += (srcPtr[2]<<16);
    value32 += (srcPtr[3]<<24);
    return value32;
}

/***************************************
*   Legacy Compression
***************************************/

/* unoptimized version; solves endianess & alignment issues */
static void LZ4G_writeLE32 (void* p, unsigned value32)
{
    unsigned char* dstPtr = (unsigned char*)p;
    dstPtr[0] = (unsigned char)value32;
    dstPtr[1] = (unsigned char)(value32 >> 8);
    dstPtr[2] = (unsigned char)(value32 >> 16);
    dstPtr[3] = (unsigned char)(value32 >> 24);
}

static int LZ4G_decodeLegacyStream(FILE* finput, FILE* foutput, unsigned long long* ret, char** errstring, int* nerrbytes)
{
    unsigned long long filesize = 0;
    char* in_buff;
    char* out_buff;

    /* Allocate Memory */
    in_buff = (char*)malloc(LZ4_compressBound(LEGACY_BLOCKSIZE));
    out_buff = (char*)malloc(LEGACY_BLOCKSIZE);
    if (!in_buff || !out_buff) LZ4G_RETURN_ERROR(51, "Allocation error : not enough memory");

    /* Main Loop */
    while (1)
    {
        int decodeSize;
        size_t sizeCheck;
        unsigned int blockSize;

        /* Block Size */
        sizeCheck = fread(in_buff, 1, 4, finput);
        if (sizeCheck==0) break;                   /* Nothing to read : file read is completed */
        blockSize = LZ4G_readLE32(in_buff);       /* Convert to Little Endian */
        if (blockSize > LZ4_COMPRESSBOUND(LEGACY_BLOCKSIZE))
        {   /* Cannot read next block : maybe new stream ? */
            fseek(finput, -4, SEEK_CUR);
            break;
        }

        /* Read Block */
        sizeCheck = fread(in_buff, 1, blockSize, finput);
        if (sizeCheck!=blockSize) LZ4G_RETURN_ERROR(52, "Read error : cannot access compressed block !");

        /* Decode Block */
        decodeSize = LZ4_decompress_safe(in_buff, out_buff, blockSize, LEGACY_BLOCKSIZE);
        if (decodeSize < 0) LZ4G_RETURN_ERROR(53, "Decoding Failed ! Corrupted input detected !");
        filesize += decodeSize;

        /* Write Block */
        sizeCheck = fwrite(out_buff, 1, decodeSize, foutput);
        if (sizeCheck != (size_t)decodeSize) LZ4G_RETURN_ERROR(54, "Write error : cannot write decoded block into output\n");
    }

    /* Free */
    free(in_buff);
    free(out_buff);

    *ret = filesize;
    return 0;
}

static int LZ4G_decodeLZ4S(FILE* finput, FILE* foutput,  unsigned long long* ret, char** errstring, int* nerrbytes)
{
    unsigned long long filesize = 0;
    void* inBuff;
    void* outBuff;
#   define HEADERMAX 20
    char  headerBuff[HEADERMAX];
    size_t sizeCheck;
    const size_t inBuffSize = 256 KB;
    const size_t outBuffSize = 256 KB;
    LZ4F_decompressionContext_t ctx;
    LZ4F_errorCode_t errorCode;
    unsigned storedSkips = 0;

    /* init */
    errorCode = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
    if (LZ4F_isError(errorCode)) LZ4G_RETURN_ERROR_DOTS(60, "Can't create context : %s", LZ4F_getErrorName(errorCode));
    LZ4G_writeLE32(headerBuff, LZ4G_MAGICNUMBER);   /* regenerated here, as it was already read from finput */

    /* Allocate Memory */
    inBuff = malloc(256 KB);
    outBuff = malloc(256 KB);
    if (!inBuff || !outBuff) LZ4G_RETURN_ERROR(61, "Allocation error : not enough memory");

    /* Init feed with magic number (already consumed from FILE) */
    {
        size_t inSize = 4;
        size_t outSize=0;
        LZ4G_writeLE32(inBuff, LZ4G_MAGICNUMBER);
        errorCode = LZ4F_decompress(ctx, outBuff, &outSize, inBuff, &inSize, NULL);
        if (LZ4F_isError(errorCode)) LZ4G_RETURN_ERROR_DOTS(62, "Header error : %s", LZ4F_getErrorName(errorCode));
    }


    /* Main Loop */
    for (;;)
    {
        size_t readSize;
        size_t pos = 0;

        /* Read input */
        readSize = fread(inBuff, 1, inBuffSize, finput);
        if (!readSize) break;   /* empty file or stream */

        while (pos < readSize)
        {
            /* Decode Input (at least partially) */
            size_t remaining = readSize - pos;
            size_t decodedBytes = outBuffSize;
            errorCode = LZ4F_decompress(ctx, outBuff, &decodedBytes, (char*)inBuff+pos, &remaining, NULL);
            if (LZ4F_isError(errorCode)) LZ4G_RETURN_ERROR_DOTS(66, "Decompression error : %s", LZ4F_getErrorName(errorCode));
            pos += remaining;

            if (decodedBytes)
            {
                /* Write Block */
                filesize += decodedBytes;
                if (g_sparseFileSupport)
                {
                    size_t* const oBuffStartT = (size_t*)outBuff;   /* since outBuff is malloc'ed, it's aligned on size_t */
                    size_t* oBuffPosT = oBuffStartT;
                    size_t  oBuffSizeT = decodedBytes / sizeT;
                    size_t* const oBuffEndT = oBuffStartT + oBuffSizeT;
                    static const size_t bs0T = (32 KB) / sizeT;
                    while (oBuffPosT < oBuffEndT)
                    {
                        size_t seg0SizeT = bs0T;
                        size_t nb0T;
                        int seekResult;
                        if (seg0SizeT > oBuffSizeT) seg0SizeT = oBuffSizeT;
                        oBuffSizeT -= seg0SizeT;
                        for (nb0T=0; (nb0T < seg0SizeT) && (oBuffPosT[nb0T] == 0); nb0T++) ;
                        storedSkips += (unsigned)(nb0T * sizeT);
                        if (storedSkips > 1 GB)   /* avoid int overflow */
                        {
                            seekResult = fseek(foutput, 1 GB, SEEK_CUR);
                            if (seekResult != 0) LZ4G_RETURN_ERROR(68, "1 GB skip error (sparse file)");
                            storedSkips -= 1 GB;
                        }
                        if (nb0T != seg0SizeT)   /* not all 0s */
                        {
                            seekResult = fseek(foutput, storedSkips, SEEK_CUR);
                            if (seekResult) LZ4G_RETURN_ERROR(68, "Skip error (sparse file)");
                            storedSkips = 0;
                            seg0SizeT -= nb0T;
                            oBuffPosT += nb0T;
                            sizeCheck = fwrite(oBuffPosT, sizeT, seg0SizeT, foutput);
                            if (sizeCheck != seg0SizeT) LZ4G_RETURN_ERROR(68, "Write error : cannot write decoded block");
                        }
                        oBuffPosT += seg0SizeT;
                    }
                    if (decodedBytes & maskT)   /* size not multiple of sizeT (necessarily end of block) */
                    {
                        const char* const restStart = (char*)oBuffEndT;
                        const char* restPtr = restStart;
                        size_t  restSize =  decodedBytes & maskT;
                        const char* const restEnd = restStart + restSize;
                        for (; (restPtr < restEnd) && (*restPtr == 0); restPtr++) ;
                        storedSkips += (unsigned) (restPtr - restStart);
                        if (restPtr != restEnd)
                        {
                            int seekResult = fseek(foutput, storedSkips, SEEK_CUR);
                            if (seekResult) LZ4G_RETURN_ERROR(68, "Skip error (end of block)");
                            storedSkips = 0;
                            sizeCheck = fwrite(restPtr, 1, restEnd - restPtr, foutput);
                            if (sizeCheck != (size_t)(restEnd - restPtr)) LZ4G_RETURN_ERROR(68, "Write error : cannot write decoded end of block");
                        }
                    }
                }
                else
                {
                    sizeCheck = fwrite(outBuff, 1, decodedBytes, foutput);
                    if (sizeCheck != decodedBytes) LZ4G_RETURN_ERROR(68, "Write error : cannot write decoded block");
                }
            }
        }

    }

    if ((g_sparseFileSupport) && (storedSkips>0))
    {
        int seekResult;
        storedSkips --;
        seekResult = fseek(foutput, storedSkips, SEEK_CUR);
        if (seekResult != 0) LZ4G_RETURN_ERROR(69, "Final skip error (sparse file)\n");
        memset(outBuff, 0, 1);
        sizeCheck = fwrite(outBuff, 1, 1, foutput);
        if (sizeCheck != 1) LZ4G_RETURN_ERROR(69, "Write error : cannot write last zero\n");
    }

    /* Free */
    free(inBuff);
    free(outBuff);
    errorCode = LZ4F_freeDecompressionContext(ctx);
    if (LZ4F_isError(errorCode)) LZ4G_RETURN_ERROR_DOTS(69, "Error : can't free LZ4F context resource : %s", LZ4F_getErrorName(errorCode));

    *ret = filesize;
    return 0;
}


static int LZ4G_passThrough(FILE* finput, FILE* foutput, unsigned char U32store[MAGICNUMBER_SIZE],   unsigned long long* ret, char** errstring, int* nerrbytes)
{
    void* buffer = malloc(64 KB);
    size_t read = 1, sizeCheck;
    unsigned long long total = MAGICNUMBER_SIZE;

    sizeCheck = fwrite(U32store, 1, MAGICNUMBER_SIZE, foutput);
    if (sizeCheck != MAGICNUMBER_SIZE) LZ4G_RETURN_ERROR(50, "Pass-through error at start");

    while (read)
    {
        read = fread(buffer, 1, 64 KB, finput);
        total += read;
        sizeCheck = fwrite(buffer, 1, read, foutput);
        if (sizeCheck != read) LZ4G_RETURN_ERROR(50, "Pass-through error");
    }

    free(buffer);
    *ret = total;
    return 0;
}



#define ENDOFSTREAM ((unsigned long long)-1)
static int LZ4G_selectDecoder( FILE* finput,  FILE* foutput, unsigned long long* ret, char** errstring, int* nerrbytes)
{
    unsigned char U32store[MAGICNUMBER_SIZE];
    unsigned magicNumber, size;
    int errorNb;
    size_t nbReadBytes;
    static unsigned nbCalls = 0;

    /* init */
    nbCalls++;

    /* Check Archive Header */
    nbReadBytes = fread(U32store, 1, MAGICNUMBER_SIZE, finput);
    if (nbReadBytes==0) {
      *ret = ENDOFSTREAM;                  /* EOF */
      return 0;
    }
    if (nbReadBytes != MAGICNUMBER_SIZE) LZ4G_RETURN_ERROR(40, "Unrecognized header : Magic Number unreadable");
    magicNumber = LZ4G_readLE32(U32store);   /* Little Endian format */
    if (LZ4G_isSkippableMagicNumber(magicNumber)) magicNumber = LZ4G_SKIPPABLE0;  /* fold skippable magic numbers */

    switch(magicNumber)
    {
    case LZ4G_MAGICNUMBER:
        return LZ4G_DEFAULT_DECOMPRESSOR(finput, foutput, ret, errstring, nerrbytes);
    case LEGACY_MAGICNUMBER:
        /*DISPLAYLEVEL(4, "Detected : Legacy format \n");*/
        return LZ4G_decodeLegacyStream(finput, foutput, ret, errstring, nerrbytes);
    case LZ4G_SKIPPABLE0:
        /*DISPLAYLEVEL(4, "Skipping detected skippable area \n");*/
        nbReadBytes = fread(U32store, 1, 4, finput);
        if (nbReadBytes != 4) LZ4G_RETURN_ERROR(42, "Stream error : skippable size unreadable");
        size = LZ4G_readLE32(U32store);     /* Little Endian format */
        errorNb = fseek(finput, size, SEEK_CUR);
        if (errorNb != 0) LZ4G_RETURN_ERROR(43, "Stream error : cannot skip skippable area");
        return LZ4G_selectDecoder(finput, foutput, ret, errstring, nerrbytes);
    EXTENDED_FORMAT;
    default:
        if (nbCalls == 1)   /* just started */
        {
          if (g_overwrite) {
            return LZ4G_passThrough(finput, foutput, U32store, ret, errstring, nerrbytes);
          }
          LZ4G_RETURN_ERROR(44,"Unrecognized header : file cannot be decoded: Wrong magic number at the beginning of 1st stream.");
        }
        /*DISPLAYLEVEL(2, "Stream followed by unrecognized data\n");*/
        *ret = ENDOFSTREAM;
        return 0;
    }
}





/**************************************
 * LZ4G: Super simple API usable by Golang/other libraries for framed lz4 compression.
 *
 * both LZ4G_compressFramedFileStream() and LZ4G_decompressFramedFileStream()
 * return 0 on no error. Otherwise they return > 0 and write error
 * message to to *errstring, which must be 1024 or more bytes, allocated
 * by the caller. *nerrbytes must point to an integer which holds
 * the remaining capacity of *errstring. Hence *nerrbytes must
 * be 1024 or greater on entry.
 * ************************************/

int LZ4G_compressFramedFileStream(FILE* finput, FILE* foutput, int compressionLevel, char** errstring, int* nerrbytes) 
{
    unsigned long long filesize = 0;
    unsigned long long compressedfilesize = 0;
    char* in_buff;
    char* out_buff;
    clock_t start, end;
    int blockSize;
    size_t sizeCheck, headerSize, readSize, outBuffSize;
    LZ4F_compressionContext_t ctx;
    LZ4F_errorCode_t errorCode;
    LZ4F_preferences_t prefs;


    /* Init */
    start = clock();
    memset(&prefs, 0, sizeof(prefs));
    /*if ((g_displayLevel==2) && (compressionLevel>=3)) g_displayLevel=3;*/
    errorCode = LZ4F_createCompressionContext(&ctx, LZ4F_VERSION);
    if (LZ4F_isError(errorCode)) LZ4G_RETURN_ERROR_DOTS(30, "Allocation error : can't create LZ4F context : '%s'", LZ4F_getErrorName(errorCode));
    blockSize = LZ4G_GetBlockSize_FromBlockId (g_blockSizeId);

    /* Set compression parameters */
    prefs.autoFlush = 1;
    prefs.compressionLevel = compressionLevel;
    prefs.frameInfo.blockMode = (blockMode_t)g_blockIndependence;
    prefs.frameInfo.blockSizeID = (blockSizeID_t)g_blockSizeId;
    prefs.frameInfo.contentChecksumFlag = (contentChecksum_t)g_streamChecksum;
    if (g_contentSizeFlag)
    {
      unsigned long long fileSize = 0; /*LZ4G_GetFileSize(input_filename);*/
      prefs.frameInfo.contentSize = fileSize;   /* == 0 if input == stdin */
    }

    /* Allocate Memory */
    in_buff  = (char*)malloc(blockSize);
    outBuffSize = LZ4F_compressBound(blockSize, &prefs);
    out_buff = (char*)malloc(outBuffSize);
    if (!in_buff || !out_buff) LZ4G_RETURN_ERROR(31, "Allocation error : not enough memory");

    /* Write Archive Header */
    headerSize = LZ4F_compressBegin(ctx, out_buff, outBuffSize, &prefs);
    if (LZ4F_isError(headerSize)) LZ4G_RETURN_ERROR_DOTS(32, "File header generation failed : '%s'", LZ4F_getErrorName(headerSize));
    sizeCheck = fwrite(out_buff, 1, headerSize, foutput);
    if (sizeCheck!=headerSize) LZ4G_RETURN_ERROR(33, "Write error : cannot write header");
    compressedfilesize += headerSize;

    /* read first block */
    readSize = fread(in_buff, (size_t)1, (size_t)blockSize, finput);
    filesize += readSize;

    /* Main Loop */
    while (readSize>0)
    {
        size_t outSize;

        /* Compress Block */
        outSize = LZ4F_compressUpdate(ctx, out_buff, outBuffSize, in_buff, readSize, NULL);
        if (LZ4F_isError(outSize)) LZ4G_RETURN_ERROR_DOTS(34, "Compression failed : '%s'", LZ4F_getErrorName(outSize));
        compressedfilesize += outSize;

        /* Write Block */
        sizeCheck = fwrite(out_buff, 1, outSize, foutput);
        if (sizeCheck!=outSize) LZ4G_RETURN_ERROR(35, "Write error : cannot write compressed block");

        /* Read next block */
        readSize = fread(in_buff, (size_t)1, (size_t)blockSize, finput);
        filesize += readSize;
    }

    /* End of Stream mark */
    headerSize = LZ4F_compressEnd(ctx, out_buff, outBuffSize, NULL);
    if (LZ4F_isError(headerSize)) LZ4G_RETURN_ERROR_DOTS(36, "End of file generation failed : '%s'", LZ4F_getErrorName(headerSize));

    sizeCheck = fwrite(out_buff, 1, headerSize, foutput);
    if (sizeCheck!=headerSize) LZ4G_RETURN_ERROR(37, "Write error : cannot write end of stream");
    compressedfilesize += headerSize;

    /* Close & Free */
    free(in_buff);
    free(out_buff);
    fclose(finput);
    fclose(foutput);
    errorCode = LZ4F_freeCompressionContext(ctx);
    if (LZ4F_isError(errorCode)) LZ4G_RETURN_ERROR_DOTS(38, "Error : can't free LZ4F context resource : '%s'", LZ4F_getErrorName(errorCode));

    /* Final Status */
    end = clock();
    return 0;
}



/* ********************************************************************* */
/* ********************** LZ4 file-stream Decompression **************** */
/* ********************************************************************* */

int LZ4G_decompressFramedFileStream(FILE* finput, FILE* foutput, char** errstring, int* nerrbytes)
{
    unsigned long long filesize = 0, decodedSize=0;
    clock_t start, end;
    int decRes = 0;

    **errstring = '\0';
    *nerrbytes = 0;

    /* Init */
    start = clock();

    /* sparse file */
    if (g_sparseFileSupport && foutput) { SET_SPARSE_FILE_MODE(foutput); }

    /* Loop over multiple streams */
    do
    {
      decRes = LZ4G_selectDecoder(finput, foutput, &decodedSize, errstring, nerrbytes);
      if (decRes != 0) {
        return decRes;
      }
      if (decodedSize != ENDOFSTREAM)
        filesize += decodedSize;
    } while (decodedSize != ENDOFSTREAM);

    /* Final Status */
    end = clock();

    /* Close */
    fclose(finput);
    fclose(foutput);

    /*  Error status = OK */
    return 0;
}

