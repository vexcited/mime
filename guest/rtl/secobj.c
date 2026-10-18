/*
 * private object security, shared by ntdll and the kernel: the descriptor a new object gets from its parent
 * and its creator, the parts of a descriptor read and changed, and the auto inheritance conversion. The
 * defaults a token gives are asked for through MIME_SEC_DEFAULTS, so a user mode caller can query the token
 * only when something is missing and the kernel can read it in place.
 */
#define NTSYSAPI
#include "rtl.h"
#include <mime/ntrtlsec.h>

#define ACCESS_SYSTEM_SECURITY 0x01000000

typedef struct {
    UCHAR *data;
    ULONG length;
    ULONG capacity;
} BUFFER;

static BOOLEAN buffer_reserve(BUFFER *b, ULONG more)
{
    if (b->length + more <= b->capacity)
        return TRUE;
    ULONG capacity = b->capacity ? b->capacity : 256;
    while (capacity < b->length + more)
        capacity *= 2;
    UCHAR *data = MimeRtlAllocate(capacity);
    if (!data)
        return FALSE;
    if (b->data) {
        memcpy(data, b->data, b->length);
        MimeRtlFree(b->data);
    }
    b->data = data;
    b->capacity = capacity;
    return TRUE;
}

static BOOLEAN buffer_put(BUFFER *b, const void *bytes, ULONG length)
{
    if (!buffer_reserve(b, length))
        return FALSE;
    memcpy(b->data + b->length, bytes, length);
    b->length += length;
    return TRUE;
}

/* ---------------------------------------------------------------- acls */

static ULONG padded(ULONG length)
{
    return (length + 3) & ~3u;
}

static PACL acl_of(const void *sd, UCHAR which, BOOLEAN *present)
{
    PACL acl = nullptr;
    BOOLEAN defaulted;
    *present = FALSE;
    if (!sd)
        return nullptr;
    if (which)
        RtlGetDaclSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, present, &acl, &defaulted);
    else
        RtlGetSaclSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, present, &acl, &defaulted);
    return *present ? acl : nullptr;
}

static USHORT control_of(const void *sd)
{
    return sd ? ((const SECURITY_DESCRIPTOR_RELATIVE *)sd)->Control : 0;
}

/*
 * the generic rights of an ace turned into the specific ones of the object, the way the kernel does it when
 * a descriptor is set: the inherit only aces keep theirs for the children, the types with a mask are the
 * allow, deny, audit and alarm ones and their callback forms but not the object callback ones, and a right
 * the object does not have is dropped, the system access right being left only to the audit and alarm kinds.
 */
static void map_ace(UCHAR *ace, const GENERIC_MAPPING *m)
{
    UCHAR type = ace[0];
    if (!(type <= ACCESS_DENIED_CALLBACK_ACE_TYPE || type == SYSTEM_AUDIT_CALLBACK_ACE_TYPE ||
          type == SYSTEM_ALARM_CALLBACK_ACE_TYPE) ||
        (ace[1] & INHERIT_ONLY_ACE))
        return;
    ULONG mask = *(ULONG *)(ace + 4);
    if (mask & GENERIC_READ)
        mask |= m->GenericRead;
    if (mask & GENERIC_WRITE)
        mask |= m->GenericWrite;
    if (mask & GENERIC_EXECUTE)
        mask |= m->GenericExecute;
    if (mask & GENERIC_ALL)
        mask |= m->GenericAll;
    mask &= 0x0FFFFFFF;
    ULONG allowed = m->GenericAll;
    if (type > 10 || !((0x673u >> type) & 1))
        allowed |= ACCESS_SYSTEM_SECURITY;
    *(ULONG *)(ace + 4) = mask & allowed;
}

static void apply_mapping(PACL acl, const GENERIC_MAPPING *mapping)
{
    if (!acl || !mapping)
        return;
    UCHAR *ace = (UCHAR *)(acl + 1);
    for (ULONG i = 0; i < acl->AceCount; i++) {
        map_ace(ace, mapping);
        ace += *(USHORT *)(ace + 2);
    }
}

static ULONG sid_offset_of(const UCHAR *ace)
{
    UCHAR type = ace[0];
    if (type <= SYSTEM_ALARM_ACE_TYPE || type == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
        type == ACCESS_DENIED_CALLBACK_ACE_TYPE || type == SYSTEM_AUDIT_CALLBACK_ACE_TYPE ||
        type == SYSTEM_ALARM_CALLBACK_ACE_TYPE)
        return 8;
    if (type >= ACCESS_ALLOWED_OBJECT_ACE_TYPE && type <= SYSTEM_ALARM_OBJECT_ACE_TYPE) {
        ULONG flags = *(const ULONG *)(ace + 8);
        return 12 + 16 * (flags & ACE_OBJECT_TYPE_PRESENT) + 8 * (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT);
    }
    return 0;
}

static BOOLEAN is_creator_sid(const SID *sid, ULONG *which)
{
    if (sid->Revision != 1 || sid->SubAuthorityCount != 1 || sid->IdentifierAuthority.Value[5] != 3)
        return FALSE;
    for (int i = 0; i < 5; i++)
        if (sid->IdentifierAuthority.Value[i])
            return FALSE;
    *which = sid->SubAuthority[0];
    return TRUE;
}

typedef struct {
    PSID owner;
    PSID group;
    const GENERIC_MAPPING *mapping;
    const GUID **types;
    ULONG type_count;
    BOOLEAN container;
    BOOLEAN auto_inherit;
} INHERIT;

/* the ace as the new object gets it, with its mask mapped and creator sids replaced. returns false when none. */
static BOOLEAN effective_ace(const INHERIT *in, const UCHAR *ace, BUFFER *out, BOOLEAN *changed)
{
    UCHAR type = ace[0];
    ULONG size = *(const USHORT *)(ace + 2);
    *changed = FALSE;
    out->length = 0;
    ULONG sid_offset = sid_offset_of(ace);
    if (!sid_offset || type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE) {
        buffer_put(out, ace, size);
        return out->data != nullptr;
    }

    ULONG mask = *(const ULONG *)(ace + 4);
    const GENERIC_MAPPING *m = in->mapping;
    if (m) {
        ULONG wide = mask;
        if (wide & GENERIC_READ)
            wide |= m->GenericRead;
        if (wide & GENERIC_WRITE)
            wide |= m->GenericWrite;
        if (wide & GENERIC_EXECUTE)
            wide |= m->GenericExecute;
        if (wide & GENERIC_ALL)
            wide |= m->GenericAll;
        /* the system access right only matters to the audit types. */
        ULONG allowed = m->GenericAll;
        if (!((0x673u >> type) & 1))
            allowed |= 0x01000000;
        ULONG mapped = wide & allowed & 0x0FFFFFFF;
        *changed = mapped != mask;
        mask = mapped & 0x011FFFFF;
    } else {
        mask &= 0x011FFFFF;
        *changed = mask != *(const ULONG *)(ace + 4);
    }

    const SID *sid = (const SID *)(ace + sid_offset);
    ULONG sid_size = RtlLengthSid((PSID)sid);
    ULONG which;
    PSID replacement = nullptr;
    if (is_creator_sid(sid, &which)) {
        replacement = which == 0 || which == 2 ? in->owner : which == 1 || which == 3 ? in->group : nullptr;
    } else if (!mask) {
        return FALSE;
    }

    /* an object ace for another kind of object does not apply, and one that does loses its inherited type. */
    ULONG head = sid_offset;
    UCHAR new_type = type;
    UCHAR head_bytes[48];
    memcpy(head_bytes, ace, head);
    BOOLEAN object = type >= ACCESS_ALLOWED_OBJECT_ACE_TYPE && type <= SYSTEM_ALARM_OBJECT_ACE_TYPE;
    if (object) {
        ULONG flags = *(const ULONG *)(ace + 8);
        if (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) {
            const GUID *inherited = (const GUID *)(ace + 12 + 16 * (flags & ACE_OBJECT_TYPE_PRESENT));
            BOOLEAN match = FALSE;
            for (ULONG i = 0; i < in->type_count && !match; i++)
                match = in->types[i] && memcmp(in->types[i], inherited, sizeof(GUID)) == 0;
            if (!match)
                return FALSE;
            *changed = TRUE;
            if (flags & ACE_OBJECT_TYPE_PRESENT) {
                *(ULONG *)(head_bytes + 8) = flags & ~ACE_INHERITED_OBJECT_TYPE_PRESENT;
                head -= 16;
            } else {
                /* allowed, denied, audit and alarm, in the order of their object forms. */
                new_type = (UCHAR)(type - ACCESS_ALLOWED_OBJECT_ACE_TYPE);
                head = 8;
            }
        }
    }

    const UCHAR *tail = ace + sid_offset + sid_size;
    ULONG tail_size = object ? 0 : size - sid_offset - sid_size;
    if (replacement) {
        sid = replacement;
        sid_size = RtlLengthSid(replacement);
        *changed = TRUE;
    }
    ULONG total = head + sid_size + tail_size;
    if (total > 0xFFFF || !buffer_reserve(out, total))
        return FALSE;
    memcpy(out->data, head_bytes, head);
    out->data[0] = new_type;
    *(ULONG *)(out->data + 4) = mask;
    memcpy(out->data + head, sid, sid_size);
    if (tail_size)
        memcpy(out->data + head + sid_size, tail, tail_size);
    *(USHORT *)(out->data + 2) = (USHORT)total;
    out->length = total;
    return TRUE;
}

/* the aces of a parent list that reach a new object, appended to the buffer. */
static NTSTATUS inherit_aces(const INHERIT *in, const ACL *parent, BUFFER *out, USHORT *count, UCHAR *revision)
{
    BUFFER scratch = {};
    NTSTATUS status = STATUS_SUCCESS;
    const UCHAR *ace = (const UCHAR *)(parent + 1);
    for (ULONG i = 0; i < parent->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
        UCHAR flags = ace[1];
        /* the ace is for the new object itself when it names its kind, and still goes on to the children. */
        BOOLEAN applies = (flags & (in->container ? CONTAINER_INHERIT_ACE : OBJECT_INHERIT_ACE)) != 0;
        BOOLEAN propagate = in->container && !(flags & NO_PROPAGATE_INHERIT_ACE) &&
                            (flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE));
        if (!applies && !propagate)
            continue;
        BOOLEAN changed = FALSE;
        BOOLEAN have = applies && effective_ace(in, ace, &scratch, &changed);
        if (have && !scratch.data) {
            status = STATUS_NO_MEMORY;
            break;
        }
        ULONG start = out->length;
        if (have) {
            scratch.data[1] = (UCHAR)(scratch.data[1] & 0xE0);
            if (in->auto_inherit)
                scratch.data[1] |= INHERITED_ACE;
            /* the ace carries the inheritance itself unless it had to be rewritten. */
            if (propagate && !changed) {
                scratch.data[1] |= flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
            }
            if (!buffer_put(out, scratch.data, scratch.length)) {
                status = STATUS_NO_MEMORY;
                break;
            }
            (*count)++;
        }
        if (propagate && (!have || changed)) {
            ULONG size = *(const USHORT *)(ace + 2);
            if (!(ace[0] <= SYSTEM_ALARM_OBJECT_ACE_TYPE && *(const ULONG *)(ace + 4) == 0)) {
                if (!buffer_put(out, ace, size)) {
                    status = STATUS_NO_MEMORY;
                    break;
                }
                out->data[out->length - size + 1] |= INHERIT_ONLY_ACE;
                if (in->auto_inherit)
                    out->data[out->length - size + 1] |= INHERITED_ACE;
                (*count)++;
            }
        }
        for (ULONG at = start; at < out->length; at += *(USHORT *)(out->data + at + 2)) {
            UCHAR type = out->data[at];
            UCHAR needed = type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE ? 3 : 0;
            if (type >= ACCESS_ALLOWED_OBJECT_ACE_TYPE && type <= SYSTEM_ALARM_OBJECT_ACE_TYPE)
                needed = ACL_REVISION_DS;
            if (needed > *revision)
                *revision = needed;
        }
    }
    if (scratch.data)
        MimeRtlFree(scratch.data);
    return status;
}

/* an acl made of the aces in the buffer, in a block of its own. */
static PACL acl_from(BUFFER *aces, USHORT count, UCHAR revision)
{
    ULONG size = padded(8 + aces->length);
    if (size > 0xFFFF)
        return nullptr;
    PACL acl = MimeRtlAllocate(size);
    if (!acl)
        return nullptr;
    memset(acl, 0, size);
    acl->AclRevision = revision;
    acl->AclSize = (USHORT)size;
    acl->AceCount = count;
    if (aces->length)
        memcpy(acl + 1, aces->data, aces->length);
    return acl;
}

/* the aces of a list without the inherited ones, as explicit aces to keep. */
static BOOLEAN keep_explicit(BUFFER *out, const ACL *acl, USHORT *count, UCHAR *revision)
{
    const UCHAR *ace = (const UCHAR *)(acl + 1);
    for (ULONG i = 0; i < acl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
        if (ace[1] & INHERITED_ACE)
            continue;
        if (!buffer_put(out, ace, *(const USHORT *)(ace + 2)))
            return FALSE;
        (*count)++;
    }
    if (acl->AclRevision > *revision)
        *revision = acl->AclRevision;
    return TRUE;
}

/* ---------------------------------------------------------------- descriptors */

typedef struct {
    PACL sacl;
    PACL dacl;
    PSID owner;
    PSID group;
    USHORT control;
    UCHAR rm_control;
} PARTS;

/* a self relative descriptor of the parts: sacl, dacl, owner and group in that order. */
static NTSTATUS make_relative(const PARTS *p, PSECURITY_DESCRIPTOR *result)
{
    ULONG sacl = p->sacl ? padded(p->sacl->AclSize) : 0;
    ULONG dacl = p->dacl ? padded(p->dacl->AclSize) : 0;
    ULONG owner = p->owner ? padded(RtlLengthSid(p->owner)) : 0;
    ULONG group = p->group ? padded(RtlLengthSid(p->group)) : 0;
    UCHAR *sd = MimeRtlAllocate(20 + sacl + dacl + owner + group);
    if (!sd)
        return STATUS_NO_MEMORY;
    memset(sd, 0, 20);
    SECURITY_DESCRIPTOR_RELATIVE *r = (SECURITY_DESCRIPTOR_RELATIVE *)sd;
    r->Revision = SECURITY_DESCRIPTOR_REVISION;
    r->Control = p->control | SE_SELF_RELATIVE;
    if (p->control & SE_RM_CONTROL_VALID)
        r->Sbz1 = p->rm_control;
    ULONG at = 20;
    if (p->sacl) {
        memcpy(sd + at, p->sacl, p->sacl->AclSize);
        memset(sd + at + p->sacl->AclSize, 0, sacl - p->sacl->AclSize);
        r->Sacl = at;
        at += sacl;
    }
    if (p->dacl) {
        memcpy(sd + at, p->dacl, p->dacl->AclSize);
        memset(sd + at + p->dacl->AclSize, 0, dacl - p->dacl->AclSize);
        r->Dacl = at;
        at += dacl;
    }
    if (p->owner) {
        ULONG length = RtlLengthSid(p->owner);
        memcpy(sd + at, p->owner, length);
        memset(sd + at + length, 0, owner - length);
        r->Owner = at;
        at += owner;
    }
    if (p->group) {
        ULONG length = RtlLengthSid(p->group);
        memcpy(sd + at, p->group, length);
        memset(sd + at + length, 0, group - length);
        r->Group = at;
    }
    *result = sd;
    return STATUS_SUCCESS;
}

static void free_acl(PACL acl, BOOLEAN owned)
{
    if (acl && owned)
        MimeRtlFree(acl);
}

/* a block of its own with the acl in it, its generic rights mapped. */
static PACL copy_acl(const ACL *acl, const GENERIC_MAPPING *mapping)
{
    if (!acl)
        return nullptr;
    PACL copy = MimeRtlAllocate(padded(acl->AclSize));
    if (!copy)
        return nullptr;
    memset(copy, 0, padded(acl->AclSize));
    memcpy(copy, acl, acl->AclSize);
    apply_mapping(copy, mapping);
    return copy;
}

/*
 * one of the two acls of a new object. without auto inheritance an acl the creator gave is all there is, else
 * the creator's explicit aces come first, then what the parent hands down. returns false when there is
 * nothing, so the caller falls back to a default.
 */
static NTSTATUS build_list(INHERIT *in, const ACL *creator_acl, BOOLEAN creator_present, BOOLEAN creator_default,
                           BOOLEAN protect, const ACL *parent_acl, PACL *out, BOOLEAN *present)
{
    BOOLEAN explicit_present = creator_present && !creator_default;
    BUFFER aces = {};
    USHORT count = 0;
    UCHAR revision = ACL_REVISION;
    NTSTATUS status = STATUS_SUCCESS;
    *out = nullptr;
    *present = FALSE;

    if (explicit_present && !in->auto_inherit) {
        *out = copy_acl(creator_acl, in->mapping);
        if (creator_acl && !*out)
            return STATUS_NO_MEMORY;
        *present = TRUE;
        return STATUS_SUCCESS;
    }
    if (explicit_present && creator_acl) {
        if (!keep_explicit(&aces, creator_acl, &count, &revision))
            return STATUS_NO_MEMORY;
        if (in->mapping) {
            UCHAR *ace = aces.data;
            for (USHORT i = 0; i < count; i++, ace += *(USHORT *)(ace + 2))
                map_ace(ace, in->mapping);
        }
    }
    if (!protect && parent_acl)
        status = inherit_aces(in, parent_acl, &aces, &count, &revision);
    if (NT_SUCCESS(status) && (count || (explicit_present && creator_acl))) {
        if (8 + aces.length > 0xFFFF) {
            status = STATUS_BAD_INHERITANCE_ACL;
        } else {
            *out = acl_from(&aces, count, revision);
            status = *out ? STATUS_SUCCESS : STATUS_NO_MEMORY;
            *present = *out != nullptr;
        }
    } else if (NT_SUCCESS(status) && explicit_present) {
        *present = TRUE;
    }
    if (aces.data)
        MimeRtlFree(aces.data);
    return status;
}


NTSTATUS MimeSecNewObject(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator, PSECURITY_DESCRIPTOR *object,
                          const GUID **types, ULONG type_count, BOOLEAN container, ULONG flags,
                          const GENERIC_MAPPING *mapping, const MIME_SEC_DEFAULTS *defaults)
{
    if (!object)
        return STATUS_INVALID_PARAMETER;
    if ((parent && !RtlValidSecurityDescriptor(parent)) || (creator && !RtlValidSecurityDescriptor(creator)))
        return STATUS_INVALID_PARAMETER;

    BOOLEAN creator_default = (flags & SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT) != 0;
    USHORT creator_control = control_of(creator);
    BOOLEAN creator_has_dacl, creator_has_sacl, parent_has_dacl, parent_has_sacl;
    PACL creator_dacl = acl_of(creator, 1, &creator_has_dacl);
    PACL creator_sacl = acl_of(creator, 0, &creator_has_sacl);
    PACL parent_dacl = acl_of(parent, 1, &parent_has_dacl);
    PACL parent_sacl = acl_of(parent, 0, &parent_has_sacl);

    PSID owner = nullptr, group = nullptr;
    BOOLEAN defaulted;
    if (creator) {
        RtlGetOwnerSecurityDescriptor(creator, &owner, &defaulted);
        RtlGetGroupSecurityDescriptor(creator, &group, &defaulted);
    }
    if (!owner && (flags & SEF_DEFAULT_OWNER_FROM_PARENT) && parent)
        RtlGetOwnerSecurityDescriptor(parent, &owner, &defaulted);
    if (!group && (flags & SEF_DEFAULT_GROUP_FROM_PARENT) && parent)
        RtlGetGroupSecurityDescriptor(parent, &group, &defaulted);

    /* the token only matters for what nothing else decided. */
    PARTS parts = {};
    PACL dacl = nullptr, sacl = nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    if (!owner)
        status = defaults ? defaults->Fetch(defaults->Context, MIME_SEC_OWNER, (PVOID *)&owner) : STATUS_NO_TOKEN;
    if (NT_SUCCESS(status) && !group)
        status = defaults ? defaults->Fetch(defaults->Context, MIME_SEC_GROUP, (PVOID *)&group) : STATUS_NO_TOKEN;
    if (!NT_SUCCESS(status))
        return status;

    INHERIT in = {owner, group, mapping, types, type_count, container, FALSE};

    BOOLEAN dacl_present = FALSE, sacl_present = FALSE;
    in.auto_inherit = (flags & SEF_DACL_AUTO_INHERIT) != 0;
    BOOLEAN protect = in.auto_inherit && (creator_control & SE_DACL_PROTECTED);
    status = build_list(&in, creator_dacl, creator_has_dacl, creator_default, protect, parent_dacl, &dacl,
                        &dacl_present);
    if (NT_SUCCESS(status) && !dacl_present && protect && !creator_has_dacl) {
        /* a list that is protected and was not given has nothing to inherit and nothing to default to: everyone. */
        dacl_present = TRUE;
    } else if (NT_SUCCESS(status) && !dacl_present) {
        /* the default is what the creator offered as one, else the token's. */
        PACL source = nullptr;
        if (creator_has_dacl && creator_default) {
            source = creator_dacl;
            dacl_present = TRUE;
        } else if (defaults) {
            PACL token_dacl = nullptr;
            if (NT_SUCCESS(defaults->Fetch(defaults->Context, MIME_SEC_DACL, (PVOID *)&token_dacl)) && token_dacl) {
                source = token_dacl;
                dacl_present = TRUE;
            }
        }
        if (source) {
            dacl = copy_acl(source, mapping);
            if (!dacl)
                status = STATUS_NO_MEMORY;
        }
    }
    if (dacl_present)
        parts.control |= SE_DACL_PRESENT;
    if (in.auto_inherit)
        parts.control |= SE_DACL_AUTO_INHERITED | (protect ? SE_DACL_PROTECTED : 0);
    if (NT_SUCCESS(status)) {
        in.auto_inherit = (flags & SEF_SACL_AUTO_INHERIT) != 0;
        protect = in.auto_inherit && (creator_control & SE_SACL_PROTECTED);
        status = build_list(&in, creator_sacl, creator_has_sacl, creator_default, protect, parent_sacl, &sacl,
                            &sacl_present);
        if (sacl_present)
            parts.control |= SE_SACL_PRESENT;
        if (in.auto_inherit && sacl_present)
            parts.control |= SE_SACL_AUTO_INHERITED | (protect ? SE_SACL_PROTECTED : 0);
    }
    if (NT_SUCCESS(status)) {
        parts.dacl = dacl;
        parts.sacl = sacl;
        parts.owner = owner;
        parts.group = group;
        if (creator_control & SE_RM_CONTROL_VALID) {
            parts.control |= SE_RM_CONTROL_VALID;
            parts.rm_control = ((const SECURITY_DESCRIPTOR_RELATIVE *)creator)->Sbz1;
        }
        status = make_relative(&parts, object);
    }
    free_acl(dacl, TRUE);
    free_acl(sacl, TRUE);
    return status;
}

NTSTATUS NTAPI RtlDeleteSecurityObject(PSECURITY_DESCRIPTOR *object)
{
    MimeRtlFree(*object);
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- query and set */

/* which part of a sacl information bit an ace belongs to. */
static ULONG sacl_kind(UCHAR type)
{
    switch (type) {
    case SYSTEM_MANDATORY_LABEL_ACE_TYPE: return LABEL_SECURITY_INFORMATION;
    case SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE: return ATTRIBUTE_SECURITY_INFORMATION;
    case SYSTEM_SCOPED_POLICY_ID_ACE_TYPE: return SCOPE_SECURITY_INFORMATION;
    case SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE: return PROCESS_TRUST_LABEL_SECURITY_INFORMATION;
    case SYSTEM_ACCESS_FILTER_ACE_TYPE: return ACCESS_FILTER_SECURITY_INFORMATION;
    default: return SACL_SECURITY_INFORMATION;
    }
}

/* the aces of the kinds the information selects. size only when there is no destination. */
ULONG MimeSecFilterSacl(const ACL *sacl, PACL out, ULONG information)
{
    ULONG size = 8;
    USHORT count = 0;
    const UCHAR *ace = (const UCHAR *)(sacl + 1);
    UCHAR *to = (UCHAR *)(out + 1);
    for (ULONG i = 0; i < sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
        if (!(information & sacl_kind(ace[0])))
            continue;
        ULONG length = *(const USHORT *)(ace + 2);
        if (out) {
            memmove(to, ace, length);
            to += length;
        }
        size += length;
        count++;
    }
    if (out) {
        *(ULONG64 *)out = *(const ULONG64 *)sacl;
        out->AclSize = (USHORT)size;
        out->AceCount = count;
    }
    return size;
}

NTSTATUS NTAPI RtlQuerySecurityObject(PSECURITY_DESCRIPTOR object, SECURITY_INFORMATION information,
                                                   PSECURITY_DESCRIPTOR result, ULONG length, PULONG needed)
{
    const SECURITY_DESCRIPTOR_RELATIVE *source = object;
    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr, sacl = nullptr;
    BOOLEAN present, defaulted;
    ULONG owner_size = 0, group_size = 0, dacl_size = 0, sacl_size = 0;
    ULONG sacl_bits = information & 0x1F8;

    if (information & GROUP_SECURITY_INFORMATION) {
        RtlGetGroupSecurityDescriptor(object, &group, &defaulted);
        if (group)
            group_size = padded(RtlLengthSid(group));
    }
    if (information & DACL_SECURITY_INFORMATION) {
        dacl = acl_of(object, 1, &present);
        if (dacl)
            dacl_size = padded(dacl->AclSize);
    }
    if (sacl_bits && (source->Control & SE_SACL_PRESENT)) {
        sacl = acl_of(object, 0, &present);
        if (sacl)
            sacl_size = padded(MimeSecFilterSacl(sacl, nullptr, sacl_bits));
    }
    if (information & OWNER_SECURITY_INFORMATION) {
        RtlGetOwnerSecurityDescriptor(object, &owner, &defaulted);
        if (owner)
            owner_size = padded(RtlLengthSid(owner));
    }
    ULONG total = 20 + sacl_size + dacl_size + owner_size + group_size;
    *needed = total;
    if (total > length || !result)
        return STATUS_BUFFER_TOO_SMALL;

    SECURITY_DESCRIPTOR_RELATIVE *r = result;
    memset(r, 0, 20);
    r->Revision = SECURITY_DESCRIPTOR_REVISION;
    r->Control = SE_SELF_RELATIVE;
    UCHAR *at = (UCHAR *)result + 20;
    if (sacl_bits && (source->Control & SE_SACL_PRESENT)) {
        if (sacl_size) {
            MimeSecFilterSacl(sacl, (PACL)at, sacl_bits);
            ULONG used = ((PACL)at)->AclSize;
            memset(at + used, 0, sacl_size - used);
            r->Sacl = 20;
            at += sacl_size;
        }
    }
    if (sacl_bits)
        r->Control |= source->Control & (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_AUTO_INHERITED | SE_SACL_PROTECTED);
    if (information & DACL_SECURITY_INFORMATION) {
        if (dacl_size) {
            memcpy(at, dacl, dacl->AclSize);
            memset(at + dacl->AclSize, 0, dacl_size - dacl->AclSize);
            r->Dacl = (ULONG)(at - (UCHAR *)result);
            at += dacl_size;
        }
        r->Control |= source->Control & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED);
    }
    if (information & OWNER_SECURITY_INFORMATION) {
        if (owner_size) {
            ULONG sid = RtlLengthSid(owner);
            memcpy(at, owner, sid);
            memset(at + sid, 0, owner_size - sid);
            r->Owner = (ULONG)(at - (UCHAR *)result);
            at += owner_size;
        }
        r->Control |= source->Control & SE_OWNER_DEFAULTED;
    }
    if (information & GROUP_SECURITY_INFORMATION) {
        if (group_size) {
            ULONG sid = RtlLengthSid(group);
            memcpy(at, group, sid);
            memset(at + sid, 0, group_size - sid);
            r->Group = (ULONG)(at - (UCHAR *)result);
        }
        r->Control |= source->Control & SE_GROUP_DEFAULTED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetSecurityObjectEx(SECURITY_INFORMATION information,
                                                   PSECURITY_DESCRIPTOR modification, PSECURITY_DESCRIPTOR *object,
                                                   ULONG flags, const GENERIC_MAPPING *mapping, HANDLE token)
{
    (void)flags;
    (void)token;
    if (!(((const SECURITY_DESCRIPTOR_RELATIVE *)*object)->Control & SE_SELF_RELATIVE))
        return STATUS_BAD_DESCRIPTOR_FORMAT;
    PARTS parts = {};
    PSID owner = nullptr, group = nullptr;
    BOOLEAN present, defaulted;
    USHORT old_control = control_of(*object);
    USHORT new_control = control_of(modification);

    if (information & OWNER_SECURITY_INFORMATION) {
        RtlGetOwnerSecurityDescriptor(modification, &owner, &defaulted);
        if (!owner || !RtlValidSid(owner))
            return STATUS_INVALID_OWNER;
    } else {
        RtlGetOwnerSecurityDescriptor(*object, &owner, &defaulted);
        if (!owner || !RtlValidSid(owner))
            return STATUS_INVALID_OWNER;
        parts.control |= old_control & SE_OWNER_DEFAULTED;
    }
    if (information & GROUP_SECURITY_INFORMATION) {
        RtlGetGroupSecurityDescriptor(modification, &group, &defaulted);
        if (!group || !RtlValidSid(group))
            return STATUS_INVALID_PRIMARY_GROUP;
    } else {
        RtlGetGroupSecurityDescriptor(*object, &group, &defaulted);
        if (!group || !RtlValidSid(group))
            return STATUS_INVALID_PRIMARY_GROUP;
        parts.control |= old_control & SE_GROUP_DEFAULTED;
    }

    PACL dacl;
    if (information & DACL_SECURITY_INFORMATION) {
        dacl = acl_of(modification, 1, &present);
        parts.control |= SE_DACL_PRESENT | (new_control & SE_DACL_PROTECTED);
        if ((new_control & (SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED)) ==
            (SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED))
            parts.control |= SE_DACL_AUTO_INHERITED;
    } else {
        dacl = acl_of(*object, 1, &present);
        parts.control |= old_control & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED);
    }

    /* the kinds of sacl ace the information names come from the modification, the others stay. */
    BUFFER merged = {};
    USHORT merged_count = 0;
    UCHAR merged_revision = ACL_REVISION;
    PACL sacl = nullptr;
    PACL owned_sacl = nullptr;
    ULONG sacl_bits = information & 0x1F8;
    PACL old_sacl = acl_of(*object, 0, &present);
    if (sacl_bits) {
        PACL new_sacl = acl_of(modification, 0, &present);
        const UCHAR *ace;
        if (old_sacl) {
            ace = (const UCHAR *)(old_sacl + 1);
            for (ULONG i = 0; i < old_sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
                if (sacl_bits & sacl_kind(ace[0]))
                    continue;
                if (!buffer_put(&merged, ace, *(const USHORT *)(ace + 2)))
                    return STATUS_NO_MEMORY;
                merged_count++;
            }
            if (old_sacl->AclRevision > merged_revision)
                merged_revision = old_sacl->AclRevision;
        }
        if (new_sacl) {
            ace = (const UCHAR *)(new_sacl + 1);
            for (ULONG i = 0; i < new_sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
                if (!(sacl_bits & sacl_kind(ace[0])))
                    continue;
                if (!buffer_put(&merged, ace, *(const USHORT *)(ace + 2)))
                    return STATUS_NO_MEMORY;
                merged_count++;
            }
            if (new_sacl->AclRevision > merged_revision)
                merged_revision = new_sacl->AclRevision;
        }
        if (merged_count || (sacl_bits & SACL_SECURITY_INFORMATION)) {
            owned_sacl = acl_from(&merged, merged_count, merged_revision);
            if (!owned_sacl) {
                if (merged.data)
                    MimeRtlFree(merged.data);
                return STATUS_NO_MEMORY;
            }
            sacl = owned_sacl;
            parts.control |= SE_SACL_PRESENT;
            if (sacl_bits & SACL_SECURITY_INFORMATION) {
                parts.control |= new_control & SE_SACL_PROTECTED;
                if ((new_control & (SE_SACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERITED)) ==
                    (SE_SACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERITED))
                    parts.control |= SE_SACL_AUTO_INHERITED;
            } else {
                parts.control |= old_control & (SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED);
            }
        }
        if (merged.data)
            MimeRtlFree(merged.data);
    } else {
        sacl = old_sacl;
        parts.control |= old_control & (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_AUTO_INHERITED | SE_SACL_PROTECTED);
    }

    PACL owned_dacl = nullptr;
    if ((information & DACL_SECURITY_INFORMATION) && dacl && mapping) {
        owned_dacl = MimeRtlAllocate(padded(dacl->AclSize));
        if (!owned_dacl) {
            free_acl(owned_sacl, TRUE);
            return STATUS_NO_MEMORY;
        }
        memset(owned_dacl, 0, padded(dacl->AclSize));
        memcpy(owned_dacl, dacl, dacl->AclSize);
        apply_mapping(owned_dacl, mapping);
        dacl = owned_dacl;
    }

    parts.sacl = sacl;
    parts.dacl = dacl;
    parts.owner = owner;
    parts.group = group;
    if (old_control & SE_RM_CONTROL_VALID) {
        parts.control |= SE_RM_CONTROL_VALID;
        parts.rm_control = ((const SECURITY_DESCRIPTOR_RELATIVE *)*object)->Sbz1;
    }
    PSECURITY_DESCRIPTOR result;
    NTSTATUS status = make_relative(&parts, &result);
    free_acl(owned_sacl, TRUE);
    free_acl(owned_dacl, TRUE);
    if (!NT_SUCCESS(status))
        return status;
    MimeRtlFree(*object);
    *object = result;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetSecurityObject(SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR modification,
                                                 PSECURITY_DESCRIPTOR *object, const GENERIC_MAPPING *mapping,
                                                 HANDLE token)
{
    return RtlSetSecurityObjectEx(information, modification, object, 0, mapping, token);
}

/* ---------------------------------------------------------------- auto inheritance */

static BOOLEAN same_ace(const UCHAR *a, const UCHAR *b)
{
    ULONG size = *(const USHORT *)(a + 2);
    if (size != *(const USHORT *)(b + 2) || a[0] != b[0])
        return FALSE;
    if ((a[1] & ~INHERITED_ACE) != (b[1] & ~INHERITED_ACE))
        return FALSE;
    return memcmp(a + 4, b + 4, size - 4) == 0;
}

/* the aces of the current list the parent would have handed down are marked inherited and moved last. */
static NTSTATUS convert_acl(const INHERIT *in, const ACL *parent, const ACL *current, PACL *result)
{
    BUFFER wanted = {}, explicit_part = {}, inherited_part = {};
    USHORT wanted_count = 0, explicit_count = 0, inherited_count = 0;
    UCHAR revision = current->AclRevision;
    NTSTATUS status = STATUS_SUCCESS;

    if (parent)
        status = inherit_aces(in, parent, &wanted, &wanted_count, &revision);
    if (NT_SUCCESS(status)) {
        const UCHAR *ace = (const UCHAR *)(current + 1);
        for (ULONG i = 0; i < current->AceCount && NT_SUCCESS(status); i++, ace += *(const USHORT *)(ace + 2)) {
            BOOLEAN match = FALSE;
            const UCHAR *candidate = wanted.data;
            for (USHORT j = 0; j < wanted_count && !match; j++, candidate += *(const USHORT *)(candidate + 2))
                match = same_ace(ace, candidate);
            ULONG size = *(const USHORT *)(ace + 2);
            BUFFER *to = match ? &inherited_part : &explicit_part;
            ULONG at = to->length;
            if (!buffer_put(to, ace, size)) {
                status = STATUS_NO_MEMORY;
                break;
            }
            if (match)
                to->data[at + 1] |= INHERITED_ACE;
            else
                to->data[at + 1] &= ~INHERITED_ACE;
            if (match)
                inherited_count++;
            else
                explicit_count++;
        }
    }
    if (NT_SUCCESS(status)) {
        if (!buffer_put(&explicit_part, inherited_part.data, inherited_part.length)) {
            status = STATUS_NO_MEMORY;
        } else {
            *result = acl_from(&explicit_part, explicit_count + inherited_count, revision);
            if (!*result)
                status = STATUS_NO_MEMORY;
        }
    }
    for (BUFFER *b = &wanted; b; b = b == &wanted ? &explicit_part : b == &explicit_part ? &inherited_part : nullptr)
        if (b->data)
            MimeRtlFree(b->data);
    return status;
}

NTSTATUS NTAPI RtlConvertToAutoInheritSecurityObject(PSECURITY_DESCRIPTOR parent,
                                                                  PSECURITY_DESCRIPTOR current,
                                                                  PSECURITY_DESCRIPTOR *result, const GUID *type,
                                                                  BOOLEAN container, const GENERIC_MAPPING *mapping)
{
    if (!current || !result || !RtlValidSecurityDescriptor(current) || (parent && !RtlValidSecurityDescriptor(parent)))
        return STATUS_INVALID_PARAMETER;
    PSID owner = nullptr, group = nullptr;
    BOOLEAN present, defaulted;
    RtlGetOwnerSecurityDescriptor(current, &owner, &defaulted);
    RtlGetGroupSecurityDescriptor(current, &group, &defaulted);
    USHORT control = control_of(current);
    BOOLEAN have_parent_dacl, have_parent_sacl;
    PACL parent_dacl = acl_of(parent, 1, &have_parent_dacl);
    PACL parent_sacl = acl_of(parent, 0, &have_parent_sacl);
    PACL dacl = acl_of(current, 1, &present);
    PACL sacl = acl_of(current, 0, &present);

    INHERIT in = {owner, group, mapping, type ? &type : nullptr, type ? 1u : 0u, container, TRUE};
    PARTS parts = {};
    parts.owner = owner;
    parts.group = group;
    parts.control = control & (SE_OWNER_DEFAULTED | SE_GROUP_DEFAULTED | SE_DACL_PRESENT | SE_DACL_DEFAULTED |
                               SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_DACL_PROTECTED | SE_SACL_PROTECTED);
    PACL new_dacl = nullptr, new_sacl = nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    if (dacl) {
        status = convert_acl(&in, (control & SE_DACL_PROTECTED) ? nullptr : parent_dacl, dacl, &new_dacl);
        parts.dacl = new_dacl;
    }
    if (NT_SUCCESS(status) && sacl) {
        status = convert_acl(&in, (control & SE_SACL_PROTECTED) ? nullptr : parent_sacl, sacl, &new_sacl);
        parts.sacl = new_sacl;
    }
    if (NT_SUCCESS(status)) {
        parts.control |= SE_DACL_AUTO_INHERITED;
        if (parts.control & SE_SACL_PRESENT)
            parts.control |= SE_SACL_AUTO_INHERITED;
        if (control & SE_RM_CONTROL_VALID) {
            parts.control |= SE_RM_CONTROL_VALID;
            parts.rm_control = ((const SECURITY_DESCRIPTOR_RELATIVE *)current)->Sbz1;
        }
        status = make_relative(&parts, result);
    }
    free_acl(new_dacl, TRUE);
    free_acl(new_sacl, TRUE);
    return status;
}
