/* the preparation of the source for each file type, and the steps after the decoding. */
#include "../crypto/crypto.h"
#include "internal.h"

enum { TYPE_RAW = 1, TYPE_CLI4_I386 = 16, TYPE_CLI4_AMD64 = 32 };

/* the executables: the description of the target in the pre-process stream, and the transforms of the source. */
static int prepare_executable(uint32_t file_type, uint64_t flags, const uint8_t *source, size_t source_size,
                              MD_BITS *preprocess, uint8_t **prepared, size_t *prepared_size, MD_RIFT *rift)
{
    if (preprocess->Failed || source_size > 0x7FFFFFFF)
        return MSDELTA_BAD;
    MD_PE_TARGET target = {};
    MD_RIFT used = {}, to_rva = {}, first = {}, second = {}, reversed = {}, cli_rift = {};
    uint8_t *copy = MdAlloc(source_size);
    MD_PE pe;
    int status = MSDELTA_MEMORY;
    if (!copy)
        return status;
    memcpy(copy, source, source_size);
    status = MSDELTA_BAD;
    if (!MdPeLoad(&pe, copy, (uint32_t)source_size))
        goto done;
    if (flags & 1ull << 13)
        MdPeUnbind(&pe);
    if (!MdPeOffsetToRva(&pe, &to_rva)) {
        status = MSDELTA_MEMORY;
        goto done;
    }
    target.ImageBase = MdBitsRead(preprocess, 64);
    target.GlobalPointer = (uint32_t)MdBitsRead(preprocess, 32);
    target.TimeStamp = (uint32_t)MdBitsRead(preprocess, 32);
    if (!MdRiftRead(preprocess, &target.RvaToOffset))
        goto done;
    /* the metadata of the target, which is empty when the flag bit is clear, then the used table and the map. */
    if (!MdCliFromBits(preprocess, &target.Cli))
        goto done;
    target.HasCli = target.Cli.Present;
    if (!MdRiftRead(preprocess, &used) || preprocess->Failed)
        goto done;
    if (!MdCliMapFromBits(preprocess, &target.Map))
        goto done;
    status = MdPeTransform(&pe, file_type, flags, &used, &target);
    if (status != MSDELTA_OK)
        goto done;
    status = MSDELTA_MEMORY;
    MD_CLI source_cli;
    if (!MdPeCli(&pe, &source_cli) || !MdPeCliCompressionRift(&source_cli, &target.Cli, &target.Map, &pe, &cli_rift))
        goto done;
    if (!MdRiftMultiply(&first, &to_rva, &used) || !MdRiftMultiply(&second, &first, &target.RvaToOffset) ||
        !MdRiftReverse(&reversed, &second, 0) || !MdRiftSum(rift, &reversed, &cli_rift))
        goto done;
    *prepared = copy;
    *prepared_size = source_size;
    copy = nullptr;
    status = MSDELTA_OK;
done:
    MsDeltaFree(copy);
    MdRiftFree(&used);
    MdRiftFree(&to_rva);
    MdRiftFree(&first);
    MdRiftFree(&second);
    MdRiftFree(&reversed);
    MdRiftFree(&cli_rift);
    MdCliMapFree(&target.Map);
    MdRiftFree(&target.RvaToOffset);
    return status;
}

int MdPrepareSource(uint32_t file_type, uint64_t flags, const uint8_t *source, size_t source_size,
                    MD_BITS *preprocess, uint64_t target_size, uint8_t **prepared, size_t *prepared_size,
                    MD_RIFT *rift)
{
    (void)target_size;
    memset(rift, 0, sizeof(*rift));
    if (file_type == TYPE_RAW) {
        *prepared = (uint8_t *)source;
        *prepared_size = source_size;
        return MSDELTA_OK;
    }
    if (file_type == TYPE_CLI4_I386 || file_type == TYPE_CLI4_AMD64)
        return prepare_executable(file_type, flags, source, source_size, preprocess, prepared, prepared_size, rift);
    return MSDELTA_UNSUPPORTED;
}

/* the calls that the compressor turned into absolute addresses get their relative ones back. */
static void translate_calls(uint8_t *data, size_t size)
{
    int32_t limit = size > 0x40000000 ? 0x40000000 : (int32_t)size;
    size_t left = (size_t)limit;
    int32_t position = 0;
    uint8_t *p = data;
    for (;; p += 5) {
        for (; left; left--, p++, position++)
            if (*p == 0xE8)
                break;
        if (left < 5)
            break;
        int32_t value = (int32_t)((uint32_t)p[1] | (uint32_t)p[2] << 8 | (uint32_t)p[3] << 16 | (uint32_t)p[4] << 24);
        if ((int64_t)value + position >= 0 && limit > value) {
            value = value < 0 ? value + limit : value - position;
            p[1] = (uint8_t)value;
            p[2] = (uint8_t)(value >> 8);
            p[3] = (uint8_t)(value >> 16);
            p[4] = (uint8_t)(value >> 24);
        }
        left -= 5;
        position += 5;
    }
}

int MdFinishTarget(const MSDELTA_INFO *info, uint8_t *target, size_t size)
{
    if (info->Flags & 1)
        translate_calls(target, size);
    const MimeHashAlg *alg = nullptr;
    switch (info->HashAlgId) {
    case 0x8003:
        alg = &mime_md5;
        break;
    case 0x8004:
        alg = &mime_sha1;
        break;
    case 0x800C:
        alg = &mime_sha256;
        break;
    default:
        return MSDELTA_UNSUPPORTED;
    }
    uint8_t digest[64];
    mime_hash(alg, target, size, digest);
    if (info->HashSize != alg->digest_size || memcmp(digest, info->Hash, alg->digest_size))
        return MSDELTA_MISMATCH;
    return MSDELTA_OK;
}
