#ifndef NDRGEN_WKSSVC_H
#define NDRGEN_WKSSVC_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct WKSTA_INFO_100 WKSTA_INFO_100;
typedef struct WKSTA_INFO_101 WKSTA_INFO_101;
typedef struct WKSTA_INFO_102 WKSTA_INFO_102;
typedef struct WKSTA_INFO_502 WKSTA_INFO_502;
typedef union WKSTA_INFO WKSTA_INFO;
typedef struct WKSTA_USER_INFO_0 WKSTA_USER_INFO_0;
typedef struct WKSTA_USER_INFO_0_CONTAINER WKSTA_USER_INFO_0_CONTAINER;
typedef struct WKSTA_USER_INFO_1 WKSTA_USER_INFO_1;
typedef struct WKSTA_USER_INFO_1_CONTAINER WKSTA_USER_INFO_1_CONTAINER;
typedef union WKSTA_USER_ENUM_UNION WKSTA_USER_ENUM_UNION;
typedef struct WKSTA_USER_ENUM_STRUCT WKSTA_USER_ENUM_STRUCT;
typedef struct WKSTA_USER_INFO_1101 WKSTA_USER_INFO_1101;
typedef union WKSTA_USER_INFO WKSTA_USER_INFO;
typedef struct WKSTA_TRANSPORT_INFO_0 WKSTA_TRANSPORT_INFO_0;
typedef struct WKSTA_TRANSPORT_INFO_0_CONTAINER WKSTA_TRANSPORT_INFO_0_CONTAINER;
typedef union WKSTA_TRANSPORT_ENUM_UNION WKSTA_TRANSPORT_ENUM_UNION;
typedef struct WKSTA_TRANSPORT_ENUM_STRUCT WKSTA_TRANSPORT_ENUM_STRUCT;
typedef struct USE_INFO_0 USE_INFO_0;
typedef struct USE_INFO_1 USE_INFO_1;
typedef struct USE_INFO_2 USE_INFO_2;
typedef struct USE_INFO_3 USE_INFO_3;
typedef union USE_INFO USE_INFO;
typedef struct USE_INFO_0_CONTAINER USE_INFO_0_CONTAINER;
typedef struct USE_INFO_1_CONTAINER USE_INFO_1_CONTAINER;
typedef struct USE_INFO_2_CONTAINER USE_INFO_2_CONTAINER;
typedef union USE_ENUM_UNION USE_ENUM_UNION;
typedef struct USE_ENUM_STRUCT USE_ENUM_STRUCT;
typedef struct STAT_WORKSTATION_0 STAT_WORKSTATION_0;

typedef LPWSTR WKSSVC_IDENTIFY_HANDLE;
struct WKSTA_INFO_100 {
    unsigned long wki100_platform_id;
    unsigned short *wki100_computername;
    unsigned short *wki100_langroup;
    unsigned long wki100_ver_major;
    unsigned long wki100_ver_minor;
};
struct WKSTA_INFO_101 {
    unsigned long wki101_platform_id;
    unsigned short *wki101_computername;
    unsigned short *wki101_langroup;
    unsigned long wki101_ver_major;
    unsigned long wki101_ver_minor;
    unsigned short *wki101_lanroot;
};
struct WKSTA_INFO_102 {
    unsigned long wki102_platform_id;
    unsigned short *wki102_computername;
    unsigned short *wki102_langroup;
    unsigned long wki102_ver_major;
    unsigned long wki102_ver_minor;
    unsigned short *wki102_lanroot;
    unsigned long wki102_logged_on_users;
};
struct WKSTA_INFO_502 {
    unsigned long wki502_char_wait;
    unsigned long wki502_collection_time;
    unsigned long wki502_maximum_collection_count;
    unsigned long wki502_keep_conn;
    unsigned long wki502_max_cmds;
    unsigned long wki502_sess_timeout;
    unsigned long wki502_siz_char_buf;
    unsigned long wki502_max_threads;
    unsigned long wki502_lock_quota;
    unsigned long wki502_lock_increment;
    unsigned long wki502_lock_maximum;
    unsigned long wki502_pipe_increment;
    unsigned long wki502_pipe_maximum;
    unsigned long wki502_cache_file_timeout;
    unsigned long wki502_dormant_file_limit;
    unsigned long wki502_read_ahead_throughput;
    unsigned long wki502_num_mailslot_buffers;
    unsigned long wki502_num_srv_announce_buffers;
    unsigned long wki502_max_illegal_datagram_events;
    unsigned long wki502_illegal_datagram_event_reset_frequency;
    unsigned long wki502_log_election_packets;
    unsigned long wki502_use_opportunistic_locking;
    unsigned long wki502_use_unlock_behind;
    unsigned long wki502_use_close_behind;
    unsigned long wki502_buf_named_pipes;
    unsigned long wki502_use_lock_read_unlock;
    unsigned long wki502_utilize_nt_caching;
    unsigned long wki502_use_raw_read;
    unsigned long wki502_use_raw_write;
    unsigned long wki502_use_write_raw_data;
    unsigned long wki502_use_encryption;
    unsigned long wki502_buf_files_deny_write;
    unsigned long wki502_buf_read_only_files;
    unsigned long wki502_force_core_create_mode;
    unsigned long wki502_use_512_byte_max_transfer;
};
union WKSTA_INFO {
    WKSTA_INFO_100 *WkstaInfo100;
    WKSTA_INFO_101 *WkstaInfo101;
    WKSTA_INFO_102 *WkstaInfo102;
    WKSTA_INFO_502 *WkstaInfo502;
};
struct WKSTA_USER_INFO_0 {
    unsigned short *wkui0_username;
};
struct WKSTA_USER_INFO_0_CONTAINER {
    unsigned long EntriesRead;
    WKSTA_USER_INFO_0 *Buffer;
};
struct WKSTA_USER_INFO_1 {
    unsigned short *wkui1_username;
    unsigned short *wkui1_logon_domain;
    unsigned short *wkui1_oth_domains;
    unsigned short *wkui1_logon_server;
};
struct WKSTA_USER_INFO_1_CONTAINER {
    unsigned long EntriesRead;
    WKSTA_USER_INFO_1 *Buffer;
};
union WKSTA_USER_ENUM_UNION {
    WKSTA_USER_INFO_0_CONTAINER *Level0;
    WKSTA_USER_INFO_1_CONTAINER *Level1;
};
struct WKSTA_USER_ENUM_STRUCT {
    unsigned long Level;
    WKSTA_USER_ENUM_UNION WkstaUserInfo;
};
typedef LPWSTR WKSSVC_IMPERSONATE_HANDLE;
struct WKSTA_USER_INFO_1101 {
    unsigned short *wkui1101_oth_domains;
};
union WKSTA_USER_INFO {
    WKSTA_USER_INFO_0 *UserInfo0;
    WKSTA_USER_INFO_1 *UserInfo1;
    WKSTA_USER_INFO_1101 *UserInfo1101;
};
struct WKSTA_TRANSPORT_INFO_0 {
    unsigned long wkti0_quality_of_service;
    unsigned long wkti0_number_of_vcs;
    unsigned short *wkti0_transport_name;
    unsigned short *wkti0_transport_address;
    unsigned long wkti0_wan_ish;
};
struct WKSTA_TRANSPORT_INFO_0_CONTAINER {
    unsigned long EntriesRead;
    WKSTA_TRANSPORT_INFO_0 *Buffer;
};
union WKSTA_TRANSPORT_ENUM_UNION {
    WKSTA_TRANSPORT_INFO_0_CONTAINER *Level0;
};
struct WKSTA_TRANSPORT_ENUM_STRUCT {
    unsigned long Level;
    WKSTA_TRANSPORT_ENUM_UNION WkstaTransportInfo;
};
struct USE_INFO_0 {
    unsigned short *ui0_local;
    unsigned short *ui0_remote;
};
struct USE_INFO_1 {
    unsigned short *ui1_local;
    unsigned short *ui1_remote;
    unsigned short *ui1_password;
    unsigned long ui1_status;
    unsigned long ui1_asg_type;
    unsigned long ui1_refcount;
    unsigned long ui1_usecount;
};
struct USE_INFO_2 {
    USE_INFO_1 ui2_useinfo;
    unsigned short *ui2_username;
    unsigned short *ui2_domainname;
};
struct USE_INFO_3 {
    USE_INFO_2 ui3_ui2;
    unsigned long ui3_flags;
};
union USE_INFO {
    USE_INFO_0 *UseInfo0;
    USE_INFO_1 *UseInfo1;
    USE_INFO_2 *UseInfo2;
    USE_INFO_3 *UseInfo3;
};
struct USE_INFO_0_CONTAINER {
    unsigned long EntriesRead;
    USE_INFO_0 *Buffer;
};
struct USE_INFO_1_CONTAINER {
    unsigned long EntriesRead;
    USE_INFO_1 *Buffer;
};
struct USE_INFO_2_CONTAINER {
    unsigned long EntriesRead;
    USE_INFO_2 *Buffer;
};
union USE_ENUM_UNION {
    USE_INFO_0_CONTAINER *Level0;
    USE_INFO_1_CONTAINER *Level1;
    USE_INFO_2_CONTAINER *Level2;
};
struct USE_ENUM_STRUCT {
    unsigned long Level;
    USE_ENUM_UNION UseInfo;
};
struct STAT_WORKSTATION_0 {
    long long StatisticsStartTime;
    long long BytesReceived;
    long long SmbsReceived;
    long long PagingReadBytesRequested;
    long long NonPagingReadBytesRequested;
    long long CacheReadBytesRequested;
    long long NetworkReadBytesRequested;
    long long BytesTransmitted;
    long long SmbsTransmitted;
    long long PagingWriteBytesRequested;
    long long NonPagingWriteBytesRequested;
    long long CacheWriteBytesRequested;
    long long NetworkWriteBytesRequested;
    unsigned long InitiallyFailedOperations;
    unsigned long FailedCompletionOperations;
    unsigned long ReadOperations;
    unsigned long RandomReadOperations;
    unsigned long ReadSmbs;
    unsigned long LargeReadSmbs;
    unsigned long SmallReadSmbs;
    unsigned long WriteOperations;
    unsigned long RandomWriteOperations;
    unsigned long WriteSmbs;
    unsigned long LargeWriteSmbs;
    unsigned long SmallWriteSmbs;
    unsigned long RawReadsDenied;
    unsigned long RawWritesDenied;
    unsigned long NetworkErrors;
    unsigned long Sessions;
    unsigned long FailedSessions;
    unsigned long Reconnects;
    unsigned long CoreConnects;
    unsigned long Lanman20Connects;
    unsigned long Lanman21Connects;
    unsigned long LanmanNtConnects;
    unsigned long ServerDisconnects;
    unsigned long HungSessions;
    unsigned long UseCount;
    unsigned long FailedUseCount;
    unsigned long CurrentCommands;
};
typedef enum NETSETUP_JOIN_STATUS { NetSetupUnknownStatus, NetSetupUnjoined, NetSetupWorkgroupName, NetSetupDomainName } NETSETUP_JOIN_STATUS;

#ifdef _WIN64
_Static_assert(sizeof(WKSSVC_IDENTIFY_HANDLE) == 8, "size of WKSSVC_IDENTIFY_HANDLE");
_Static_assert(sizeof(WKSTA_INFO_100) == 32, "size of WKSTA_INFO_100");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_platform_id) == 0, "WKSTA_INFO_100.wki100_platform_id");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_computername) == 8, "WKSTA_INFO_100.wki100_computername");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_langroup) == 16, "WKSTA_INFO_100.wki100_langroup");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_ver_major) == 24, "WKSTA_INFO_100.wki100_ver_major");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_ver_minor) == 28, "WKSTA_INFO_100.wki100_ver_minor");
_Static_assert(sizeof(WKSTA_INFO_101) == 40, "size of WKSTA_INFO_101");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_platform_id) == 0, "WKSTA_INFO_101.wki101_platform_id");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_computername) == 8, "WKSTA_INFO_101.wki101_computername");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_langroup) == 16, "WKSTA_INFO_101.wki101_langroup");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_ver_major) == 24, "WKSTA_INFO_101.wki101_ver_major");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_ver_minor) == 28, "WKSTA_INFO_101.wki101_ver_minor");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_lanroot) == 32, "WKSTA_INFO_101.wki101_lanroot");
_Static_assert(sizeof(WKSTA_INFO_102) == 48, "size of WKSTA_INFO_102");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_platform_id) == 0, "WKSTA_INFO_102.wki102_platform_id");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_computername) == 8, "WKSTA_INFO_102.wki102_computername");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_langroup) == 16, "WKSTA_INFO_102.wki102_langroup");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_ver_major) == 24, "WKSTA_INFO_102.wki102_ver_major");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_ver_minor) == 28, "WKSTA_INFO_102.wki102_ver_minor");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_lanroot) == 32, "WKSTA_INFO_102.wki102_lanroot");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_logged_on_users) == 40, "WKSTA_INFO_102.wki102_logged_on_users");
_Static_assert(sizeof(WKSTA_INFO_502) == 140, "size of WKSTA_INFO_502");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_char_wait) == 0, "WKSTA_INFO_502.wki502_char_wait");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_collection_time) == 4, "WKSTA_INFO_502.wki502_collection_time");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_maximum_collection_count) == 8, "WKSTA_INFO_502.wki502_maximum_collection_count");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_keep_conn) == 12, "WKSTA_INFO_502.wki502_keep_conn");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_max_cmds) == 16, "WKSTA_INFO_502.wki502_max_cmds");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_sess_timeout) == 20, "WKSTA_INFO_502.wki502_sess_timeout");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_siz_char_buf) == 24, "WKSTA_INFO_502.wki502_siz_char_buf");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_max_threads) == 28, "WKSTA_INFO_502.wki502_max_threads");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_lock_quota) == 32, "WKSTA_INFO_502.wki502_lock_quota");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_lock_increment) == 36, "WKSTA_INFO_502.wki502_lock_increment");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_lock_maximum) == 40, "WKSTA_INFO_502.wki502_lock_maximum");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_pipe_increment) == 44, "WKSTA_INFO_502.wki502_pipe_increment");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_pipe_maximum) == 48, "WKSTA_INFO_502.wki502_pipe_maximum");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_cache_file_timeout) == 52, "WKSTA_INFO_502.wki502_cache_file_timeout");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_dormant_file_limit) == 56, "WKSTA_INFO_502.wki502_dormant_file_limit");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_read_ahead_throughput) == 60, "WKSTA_INFO_502.wki502_read_ahead_throughput");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_num_mailslot_buffers) == 64, "WKSTA_INFO_502.wki502_num_mailslot_buffers");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_num_srv_announce_buffers) == 68, "WKSTA_INFO_502.wki502_num_srv_announce_buffers");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_max_illegal_datagram_events) == 72, "WKSTA_INFO_502.wki502_max_illegal_datagram_events");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_illegal_datagram_event_reset_frequency) == 76, "WKSTA_INFO_502.wki502_illegal_datagram_event_reset_frequency");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_log_election_packets) == 80, "WKSTA_INFO_502.wki502_log_election_packets");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_opportunistic_locking) == 84, "WKSTA_INFO_502.wki502_use_opportunistic_locking");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_unlock_behind) == 88, "WKSTA_INFO_502.wki502_use_unlock_behind");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_close_behind) == 92, "WKSTA_INFO_502.wki502_use_close_behind");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_buf_named_pipes) == 96, "WKSTA_INFO_502.wki502_buf_named_pipes");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_lock_read_unlock) == 100, "WKSTA_INFO_502.wki502_use_lock_read_unlock");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_utilize_nt_caching) == 104, "WKSTA_INFO_502.wki502_utilize_nt_caching");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_raw_read) == 108, "WKSTA_INFO_502.wki502_use_raw_read");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_raw_write) == 112, "WKSTA_INFO_502.wki502_use_raw_write");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_write_raw_data) == 116, "WKSTA_INFO_502.wki502_use_write_raw_data");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_encryption) == 120, "WKSTA_INFO_502.wki502_use_encryption");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_buf_files_deny_write) == 124, "WKSTA_INFO_502.wki502_buf_files_deny_write");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_buf_read_only_files) == 128, "WKSTA_INFO_502.wki502_buf_read_only_files");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_force_core_create_mode) == 132, "WKSTA_INFO_502.wki502_force_core_create_mode");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_512_byte_max_transfer) == 136, "WKSTA_INFO_502.wki502_use_512_byte_max_transfer");
_Static_assert(sizeof(WKSTA_INFO) == 8, "size of WKSTA_INFO");
_Static_assert(sizeof(WKSTA_USER_INFO_0) == 8, "size of WKSTA_USER_INFO_0");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_0, wkui0_username) == 0, "WKSTA_USER_INFO_0.wkui0_username");
_Static_assert(sizeof(WKSTA_USER_INFO_0_CONTAINER) == 16, "size of WKSTA_USER_INFO_0_CONTAINER");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_0_CONTAINER, EntriesRead) == 0, "WKSTA_USER_INFO_0_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_0_CONTAINER, Buffer) == 8, "WKSTA_USER_INFO_0_CONTAINER.Buffer");
_Static_assert(sizeof(WKSTA_USER_INFO_1) == 32, "size of WKSTA_USER_INFO_1");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_username) == 0, "WKSTA_USER_INFO_1.wkui1_username");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_logon_domain) == 8, "WKSTA_USER_INFO_1.wkui1_logon_domain");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_oth_domains) == 16, "WKSTA_USER_INFO_1.wkui1_oth_domains");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_logon_server) == 24, "WKSTA_USER_INFO_1.wkui1_logon_server");
_Static_assert(sizeof(WKSTA_USER_INFO_1_CONTAINER) == 16, "size of WKSTA_USER_INFO_1_CONTAINER");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1_CONTAINER, EntriesRead) == 0, "WKSTA_USER_INFO_1_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1_CONTAINER, Buffer) == 8, "WKSTA_USER_INFO_1_CONTAINER.Buffer");
_Static_assert(sizeof(WKSTA_USER_ENUM_UNION) == 8, "size of WKSTA_USER_ENUM_UNION");
_Static_assert(sizeof(WKSTA_USER_ENUM_STRUCT) == 16, "size of WKSTA_USER_ENUM_STRUCT");
_Static_assert(__builtin_offsetof(WKSTA_USER_ENUM_STRUCT, Level) == 0, "WKSTA_USER_ENUM_STRUCT.Level");
_Static_assert(__builtin_offsetof(WKSTA_USER_ENUM_STRUCT, WkstaUserInfo) == 8, "WKSTA_USER_ENUM_STRUCT.WkstaUserInfo");
_Static_assert(sizeof(WKSSVC_IMPERSONATE_HANDLE) == 8, "size of WKSSVC_IMPERSONATE_HANDLE");
_Static_assert(sizeof(WKSTA_USER_INFO_1101) == 8, "size of WKSTA_USER_INFO_1101");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1101, wkui1101_oth_domains) == 0, "WKSTA_USER_INFO_1101.wkui1101_oth_domains");
_Static_assert(sizeof(WKSTA_USER_INFO) == 8, "size of WKSTA_USER_INFO");
_Static_assert(sizeof(WKSTA_TRANSPORT_INFO_0) == 32, "size of WKSTA_TRANSPORT_INFO_0");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_quality_of_service) == 0, "WKSTA_TRANSPORT_INFO_0.wkti0_quality_of_service");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_number_of_vcs) == 4, "WKSTA_TRANSPORT_INFO_0.wkti0_number_of_vcs");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_transport_name) == 8, "WKSTA_TRANSPORT_INFO_0.wkti0_transport_name");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_transport_address) == 16, "WKSTA_TRANSPORT_INFO_0.wkti0_transport_address");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_wan_ish) == 24, "WKSTA_TRANSPORT_INFO_0.wkti0_wan_ish");
_Static_assert(sizeof(WKSTA_TRANSPORT_INFO_0_CONTAINER) == 16, "size of WKSTA_TRANSPORT_INFO_0_CONTAINER");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0_CONTAINER, EntriesRead) == 0, "WKSTA_TRANSPORT_INFO_0_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0_CONTAINER, Buffer) == 8, "WKSTA_TRANSPORT_INFO_0_CONTAINER.Buffer");
_Static_assert(sizeof(WKSTA_TRANSPORT_ENUM_UNION) == 8, "size of WKSTA_TRANSPORT_ENUM_UNION");
_Static_assert(sizeof(WKSTA_TRANSPORT_ENUM_STRUCT) == 16, "size of WKSTA_TRANSPORT_ENUM_STRUCT");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_ENUM_STRUCT, Level) == 0, "WKSTA_TRANSPORT_ENUM_STRUCT.Level");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_ENUM_STRUCT, WkstaTransportInfo) == 8, "WKSTA_TRANSPORT_ENUM_STRUCT.WkstaTransportInfo");
_Static_assert(sizeof(USE_INFO_0) == 16, "size of USE_INFO_0");
_Static_assert(__builtin_offsetof(USE_INFO_0, ui0_local) == 0, "USE_INFO_0.ui0_local");
_Static_assert(__builtin_offsetof(USE_INFO_0, ui0_remote) == 8, "USE_INFO_0.ui0_remote");
_Static_assert(sizeof(USE_INFO_1) == 40, "size of USE_INFO_1");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_local) == 0, "USE_INFO_1.ui1_local");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_remote) == 8, "USE_INFO_1.ui1_remote");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_password) == 16, "USE_INFO_1.ui1_password");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_status) == 24, "USE_INFO_1.ui1_status");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_asg_type) == 28, "USE_INFO_1.ui1_asg_type");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_refcount) == 32, "USE_INFO_1.ui1_refcount");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_usecount) == 36, "USE_INFO_1.ui1_usecount");
_Static_assert(sizeof(USE_INFO_2) == 56, "size of USE_INFO_2");
_Static_assert(__builtin_offsetof(USE_INFO_2, ui2_useinfo) == 0, "USE_INFO_2.ui2_useinfo");
_Static_assert(__builtin_offsetof(USE_INFO_2, ui2_username) == 40, "USE_INFO_2.ui2_username");
_Static_assert(__builtin_offsetof(USE_INFO_2, ui2_domainname) == 48, "USE_INFO_2.ui2_domainname");
_Static_assert(sizeof(USE_INFO_3) == 64, "size of USE_INFO_3");
_Static_assert(__builtin_offsetof(USE_INFO_3, ui3_ui2) == 0, "USE_INFO_3.ui3_ui2");
_Static_assert(__builtin_offsetof(USE_INFO_3, ui3_flags) == 56, "USE_INFO_3.ui3_flags");
_Static_assert(sizeof(USE_INFO) == 8, "size of USE_INFO");
_Static_assert(sizeof(USE_INFO_0_CONTAINER) == 16, "size of USE_INFO_0_CONTAINER");
_Static_assert(__builtin_offsetof(USE_INFO_0_CONTAINER, EntriesRead) == 0, "USE_INFO_0_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(USE_INFO_0_CONTAINER, Buffer) == 8, "USE_INFO_0_CONTAINER.Buffer");
_Static_assert(sizeof(USE_INFO_1_CONTAINER) == 16, "size of USE_INFO_1_CONTAINER");
_Static_assert(__builtin_offsetof(USE_INFO_1_CONTAINER, EntriesRead) == 0, "USE_INFO_1_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(USE_INFO_1_CONTAINER, Buffer) == 8, "USE_INFO_1_CONTAINER.Buffer");
_Static_assert(sizeof(USE_INFO_2_CONTAINER) == 16, "size of USE_INFO_2_CONTAINER");
_Static_assert(__builtin_offsetof(USE_INFO_2_CONTAINER, EntriesRead) == 0, "USE_INFO_2_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(USE_INFO_2_CONTAINER, Buffer) == 8, "USE_INFO_2_CONTAINER.Buffer");
_Static_assert(sizeof(USE_ENUM_UNION) == 8, "size of USE_ENUM_UNION");
_Static_assert(sizeof(USE_ENUM_STRUCT) == 16, "size of USE_ENUM_STRUCT");
_Static_assert(__builtin_offsetof(USE_ENUM_STRUCT, Level) == 0, "USE_ENUM_STRUCT.Level");
_Static_assert(__builtin_offsetof(USE_ENUM_STRUCT, UseInfo) == 8, "USE_ENUM_STRUCT.UseInfo");
_Static_assert(sizeof(STAT_WORKSTATION_0) == 216, "size of STAT_WORKSTATION_0");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, StatisticsStartTime) == 0, "STAT_WORKSTATION_0.StatisticsStartTime");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, BytesReceived) == 8, "STAT_WORKSTATION_0.BytesReceived");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmbsReceived) == 16, "STAT_WORKSTATION_0.SmbsReceived");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, PagingReadBytesRequested) == 24, "STAT_WORKSTATION_0.PagingReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NonPagingReadBytesRequested) == 32, "STAT_WORKSTATION_0.NonPagingReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CacheReadBytesRequested) == 40, "STAT_WORKSTATION_0.CacheReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NetworkReadBytesRequested) == 48, "STAT_WORKSTATION_0.NetworkReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, BytesTransmitted) == 56, "STAT_WORKSTATION_0.BytesTransmitted");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmbsTransmitted) == 64, "STAT_WORKSTATION_0.SmbsTransmitted");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, PagingWriteBytesRequested) == 72, "STAT_WORKSTATION_0.PagingWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NonPagingWriteBytesRequested) == 80, "STAT_WORKSTATION_0.NonPagingWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CacheWriteBytesRequested) == 88, "STAT_WORKSTATION_0.CacheWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NetworkWriteBytesRequested) == 96, "STAT_WORKSTATION_0.NetworkWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, InitiallyFailedOperations) == 104, "STAT_WORKSTATION_0.InitiallyFailedOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, FailedCompletionOperations) == 108, "STAT_WORKSTATION_0.FailedCompletionOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, ReadOperations) == 112, "STAT_WORKSTATION_0.ReadOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RandomReadOperations) == 116, "STAT_WORKSTATION_0.RandomReadOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, ReadSmbs) == 120, "STAT_WORKSTATION_0.ReadSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, LargeReadSmbs) == 124, "STAT_WORKSTATION_0.LargeReadSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmallReadSmbs) == 128, "STAT_WORKSTATION_0.SmallReadSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, WriteOperations) == 132, "STAT_WORKSTATION_0.WriteOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RandomWriteOperations) == 136, "STAT_WORKSTATION_0.RandomWriteOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, WriteSmbs) == 140, "STAT_WORKSTATION_0.WriteSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, LargeWriteSmbs) == 144, "STAT_WORKSTATION_0.LargeWriteSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmallWriteSmbs) == 148, "STAT_WORKSTATION_0.SmallWriteSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RawReadsDenied) == 152, "STAT_WORKSTATION_0.RawReadsDenied");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RawWritesDenied) == 156, "STAT_WORKSTATION_0.RawWritesDenied");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NetworkErrors) == 160, "STAT_WORKSTATION_0.NetworkErrors");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Sessions) == 164, "STAT_WORKSTATION_0.Sessions");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, FailedSessions) == 168, "STAT_WORKSTATION_0.FailedSessions");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Reconnects) == 172, "STAT_WORKSTATION_0.Reconnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CoreConnects) == 176, "STAT_WORKSTATION_0.CoreConnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Lanman20Connects) == 180, "STAT_WORKSTATION_0.Lanman20Connects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Lanman21Connects) == 184, "STAT_WORKSTATION_0.Lanman21Connects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, LanmanNtConnects) == 188, "STAT_WORKSTATION_0.LanmanNtConnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, ServerDisconnects) == 192, "STAT_WORKSTATION_0.ServerDisconnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, HungSessions) == 196, "STAT_WORKSTATION_0.HungSessions");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, UseCount) == 200, "STAT_WORKSTATION_0.UseCount");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, FailedUseCount) == 204, "STAT_WORKSTATION_0.FailedUseCount");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CurrentCommands) == 208, "STAT_WORKSTATION_0.CurrentCommands");
_Static_assert(sizeof(NETSETUP_JOIN_STATUS) == 4, "size of NETSETUP_JOIN_STATUS");
#else
_Static_assert(sizeof(WKSSVC_IDENTIFY_HANDLE) == 4, "size of WKSSVC_IDENTIFY_HANDLE");
_Static_assert(sizeof(WKSTA_INFO_100) == 20, "size of WKSTA_INFO_100");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_platform_id) == 0, "WKSTA_INFO_100.wki100_platform_id");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_computername) == 4, "WKSTA_INFO_100.wki100_computername");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_langroup) == 8, "WKSTA_INFO_100.wki100_langroup");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_ver_major) == 12, "WKSTA_INFO_100.wki100_ver_major");
_Static_assert(__builtin_offsetof(WKSTA_INFO_100, wki100_ver_minor) == 16, "WKSTA_INFO_100.wki100_ver_minor");
_Static_assert(sizeof(WKSTA_INFO_101) == 24, "size of WKSTA_INFO_101");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_platform_id) == 0, "WKSTA_INFO_101.wki101_platform_id");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_computername) == 4, "WKSTA_INFO_101.wki101_computername");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_langroup) == 8, "WKSTA_INFO_101.wki101_langroup");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_ver_major) == 12, "WKSTA_INFO_101.wki101_ver_major");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_ver_minor) == 16, "WKSTA_INFO_101.wki101_ver_minor");
_Static_assert(__builtin_offsetof(WKSTA_INFO_101, wki101_lanroot) == 20, "WKSTA_INFO_101.wki101_lanroot");
_Static_assert(sizeof(WKSTA_INFO_102) == 28, "size of WKSTA_INFO_102");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_platform_id) == 0, "WKSTA_INFO_102.wki102_platform_id");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_computername) == 4, "WKSTA_INFO_102.wki102_computername");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_langroup) == 8, "WKSTA_INFO_102.wki102_langroup");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_ver_major) == 12, "WKSTA_INFO_102.wki102_ver_major");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_ver_minor) == 16, "WKSTA_INFO_102.wki102_ver_minor");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_lanroot) == 20, "WKSTA_INFO_102.wki102_lanroot");
_Static_assert(__builtin_offsetof(WKSTA_INFO_102, wki102_logged_on_users) == 24, "WKSTA_INFO_102.wki102_logged_on_users");
_Static_assert(sizeof(WKSTA_INFO_502) == 140, "size of WKSTA_INFO_502");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_char_wait) == 0, "WKSTA_INFO_502.wki502_char_wait");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_collection_time) == 4, "WKSTA_INFO_502.wki502_collection_time");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_maximum_collection_count) == 8, "WKSTA_INFO_502.wki502_maximum_collection_count");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_keep_conn) == 12, "WKSTA_INFO_502.wki502_keep_conn");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_max_cmds) == 16, "WKSTA_INFO_502.wki502_max_cmds");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_sess_timeout) == 20, "WKSTA_INFO_502.wki502_sess_timeout");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_siz_char_buf) == 24, "WKSTA_INFO_502.wki502_siz_char_buf");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_max_threads) == 28, "WKSTA_INFO_502.wki502_max_threads");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_lock_quota) == 32, "WKSTA_INFO_502.wki502_lock_quota");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_lock_increment) == 36, "WKSTA_INFO_502.wki502_lock_increment");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_lock_maximum) == 40, "WKSTA_INFO_502.wki502_lock_maximum");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_pipe_increment) == 44, "WKSTA_INFO_502.wki502_pipe_increment");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_pipe_maximum) == 48, "WKSTA_INFO_502.wki502_pipe_maximum");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_cache_file_timeout) == 52, "WKSTA_INFO_502.wki502_cache_file_timeout");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_dormant_file_limit) == 56, "WKSTA_INFO_502.wki502_dormant_file_limit");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_read_ahead_throughput) == 60, "WKSTA_INFO_502.wki502_read_ahead_throughput");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_num_mailslot_buffers) == 64, "WKSTA_INFO_502.wki502_num_mailslot_buffers");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_num_srv_announce_buffers) == 68, "WKSTA_INFO_502.wki502_num_srv_announce_buffers");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_max_illegal_datagram_events) == 72, "WKSTA_INFO_502.wki502_max_illegal_datagram_events");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_illegal_datagram_event_reset_frequency) == 76, "WKSTA_INFO_502.wki502_illegal_datagram_event_reset_frequency");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_log_election_packets) == 80, "WKSTA_INFO_502.wki502_log_election_packets");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_opportunistic_locking) == 84, "WKSTA_INFO_502.wki502_use_opportunistic_locking");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_unlock_behind) == 88, "WKSTA_INFO_502.wki502_use_unlock_behind");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_close_behind) == 92, "WKSTA_INFO_502.wki502_use_close_behind");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_buf_named_pipes) == 96, "WKSTA_INFO_502.wki502_buf_named_pipes");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_lock_read_unlock) == 100, "WKSTA_INFO_502.wki502_use_lock_read_unlock");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_utilize_nt_caching) == 104, "WKSTA_INFO_502.wki502_utilize_nt_caching");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_raw_read) == 108, "WKSTA_INFO_502.wki502_use_raw_read");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_raw_write) == 112, "WKSTA_INFO_502.wki502_use_raw_write");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_write_raw_data) == 116, "WKSTA_INFO_502.wki502_use_write_raw_data");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_encryption) == 120, "WKSTA_INFO_502.wki502_use_encryption");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_buf_files_deny_write) == 124, "WKSTA_INFO_502.wki502_buf_files_deny_write");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_buf_read_only_files) == 128, "WKSTA_INFO_502.wki502_buf_read_only_files");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_force_core_create_mode) == 132, "WKSTA_INFO_502.wki502_force_core_create_mode");
_Static_assert(__builtin_offsetof(WKSTA_INFO_502, wki502_use_512_byte_max_transfer) == 136, "WKSTA_INFO_502.wki502_use_512_byte_max_transfer");
_Static_assert(sizeof(WKSTA_INFO) == 4, "size of WKSTA_INFO");
_Static_assert(sizeof(WKSTA_USER_INFO_0) == 4, "size of WKSTA_USER_INFO_0");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_0, wkui0_username) == 0, "WKSTA_USER_INFO_0.wkui0_username");
_Static_assert(sizeof(WKSTA_USER_INFO_0_CONTAINER) == 8, "size of WKSTA_USER_INFO_0_CONTAINER");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_0_CONTAINER, EntriesRead) == 0, "WKSTA_USER_INFO_0_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_0_CONTAINER, Buffer) == 4, "WKSTA_USER_INFO_0_CONTAINER.Buffer");
_Static_assert(sizeof(WKSTA_USER_INFO_1) == 16, "size of WKSTA_USER_INFO_1");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_username) == 0, "WKSTA_USER_INFO_1.wkui1_username");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_logon_domain) == 4, "WKSTA_USER_INFO_1.wkui1_logon_domain");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_oth_domains) == 8, "WKSTA_USER_INFO_1.wkui1_oth_domains");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1, wkui1_logon_server) == 12, "WKSTA_USER_INFO_1.wkui1_logon_server");
_Static_assert(sizeof(WKSTA_USER_INFO_1_CONTAINER) == 8, "size of WKSTA_USER_INFO_1_CONTAINER");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1_CONTAINER, EntriesRead) == 0, "WKSTA_USER_INFO_1_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1_CONTAINER, Buffer) == 4, "WKSTA_USER_INFO_1_CONTAINER.Buffer");
_Static_assert(sizeof(WKSTA_USER_ENUM_UNION) == 4, "size of WKSTA_USER_ENUM_UNION");
_Static_assert(sizeof(WKSTA_USER_ENUM_STRUCT) == 8, "size of WKSTA_USER_ENUM_STRUCT");
_Static_assert(__builtin_offsetof(WKSTA_USER_ENUM_STRUCT, Level) == 0, "WKSTA_USER_ENUM_STRUCT.Level");
_Static_assert(__builtin_offsetof(WKSTA_USER_ENUM_STRUCT, WkstaUserInfo) == 4, "WKSTA_USER_ENUM_STRUCT.WkstaUserInfo");
_Static_assert(sizeof(WKSSVC_IMPERSONATE_HANDLE) == 4, "size of WKSSVC_IMPERSONATE_HANDLE");
_Static_assert(sizeof(WKSTA_USER_INFO_1101) == 4, "size of WKSTA_USER_INFO_1101");
_Static_assert(__builtin_offsetof(WKSTA_USER_INFO_1101, wkui1101_oth_domains) == 0, "WKSTA_USER_INFO_1101.wkui1101_oth_domains");
_Static_assert(sizeof(WKSTA_USER_INFO) == 4, "size of WKSTA_USER_INFO");
_Static_assert(sizeof(WKSTA_TRANSPORT_INFO_0) == 20, "size of WKSTA_TRANSPORT_INFO_0");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_quality_of_service) == 0, "WKSTA_TRANSPORT_INFO_0.wkti0_quality_of_service");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_number_of_vcs) == 4, "WKSTA_TRANSPORT_INFO_0.wkti0_number_of_vcs");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_transport_name) == 8, "WKSTA_TRANSPORT_INFO_0.wkti0_transport_name");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_transport_address) == 12, "WKSTA_TRANSPORT_INFO_0.wkti0_transport_address");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0, wkti0_wan_ish) == 16, "WKSTA_TRANSPORT_INFO_0.wkti0_wan_ish");
_Static_assert(sizeof(WKSTA_TRANSPORT_INFO_0_CONTAINER) == 8, "size of WKSTA_TRANSPORT_INFO_0_CONTAINER");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0_CONTAINER, EntriesRead) == 0, "WKSTA_TRANSPORT_INFO_0_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_INFO_0_CONTAINER, Buffer) == 4, "WKSTA_TRANSPORT_INFO_0_CONTAINER.Buffer");
_Static_assert(sizeof(WKSTA_TRANSPORT_ENUM_UNION) == 4, "size of WKSTA_TRANSPORT_ENUM_UNION");
_Static_assert(sizeof(WKSTA_TRANSPORT_ENUM_STRUCT) == 8, "size of WKSTA_TRANSPORT_ENUM_STRUCT");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_ENUM_STRUCT, Level) == 0, "WKSTA_TRANSPORT_ENUM_STRUCT.Level");
_Static_assert(__builtin_offsetof(WKSTA_TRANSPORT_ENUM_STRUCT, WkstaTransportInfo) == 4, "WKSTA_TRANSPORT_ENUM_STRUCT.WkstaTransportInfo");
_Static_assert(sizeof(USE_INFO_0) == 8, "size of USE_INFO_0");
_Static_assert(__builtin_offsetof(USE_INFO_0, ui0_local) == 0, "USE_INFO_0.ui0_local");
_Static_assert(__builtin_offsetof(USE_INFO_0, ui0_remote) == 4, "USE_INFO_0.ui0_remote");
_Static_assert(sizeof(USE_INFO_1) == 28, "size of USE_INFO_1");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_local) == 0, "USE_INFO_1.ui1_local");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_remote) == 4, "USE_INFO_1.ui1_remote");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_password) == 8, "USE_INFO_1.ui1_password");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_status) == 12, "USE_INFO_1.ui1_status");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_asg_type) == 16, "USE_INFO_1.ui1_asg_type");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_refcount) == 20, "USE_INFO_1.ui1_refcount");
_Static_assert(__builtin_offsetof(USE_INFO_1, ui1_usecount) == 24, "USE_INFO_1.ui1_usecount");
_Static_assert(sizeof(USE_INFO_2) == 36, "size of USE_INFO_2");
_Static_assert(__builtin_offsetof(USE_INFO_2, ui2_useinfo) == 0, "USE_INFO_2.ui2_useinfo");
_Static_assert(__builtin_offsetof(USE_INFO_2, ui2_username) == 28, "USE_INFO_2.ui2_username");
_Static_assert(__builtin_offsetof(USE_INFO_2, ui2_domainname) == 32, "USE_INFO_2.ui2_domainname");
_Static_assert(sizeof(USE_INFO_3) == 40, "size of USE_INFO_3");
_Static_assert(__builtin_offsetof(USE_INFO_3, ui3_ui2) == 0, "USE_INFO_3.ui3_ui2");
_Static_assert(__builtin_offsetof(USE_INFO_3, ui3_flags) == 36, "USE_INFO_3.ui3_flags");
_Static_assert(sizeof(USE_INFO) == 4, "size of USE_INFO");
_Static_assert(sizeof(USE_INFO_0_CONTAINER) == 8, "size of USE_INFO_0_CONTAINER");
_Static_assert(__builtin_offsetof(USE_INFO_0_CONTAINER, EntriesRead) == 0, "USE_INFO_0_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(USE_INFO_0_CONTAINER, Buffer) == 4, "USE_INFO_0_CONTAINER.Buffer");
_Static_assert(sizeof(USE_INFO_1_CONTAINER) == 8, "size of USE_INFO_1_CONTAINER");
_Static_assert(__builtin_offsetof(USE_INFO_1_CONTAINER, EntriesRead) == 0, "USE_INFO_1_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(USE_INFO_1_CONTAINER, Buffer) == 4, "USE_INFO_1_CONTAINER.Buffer");
_Static_assert(sizeof(USE_INFO_2_CONTAINER) == 8, "size of USE_INFO_2_CONTAINER");
_Static_assert(__builtin_offsetof(USE_INFO_2_CONTAINER, EntriesRead) == 0, "USE_INFO_2_CONTAINER.EntriesRead");
_Static_assert(__builtin_offsetof(USE_INFO_2_CONTAINER, Buffer) == 4, "USE_INFO_2_CONTAINER.Buffer");
_Static_assert(sizeof(USE_ENUM_UNION) == 4, "size of USE_ENUM_UNION");
_Static_assert(sizeof(USE_ENUM_STRUCT) == 8, "size of USE_ENUM_STRUCT");
_Static_assert(__builtin_offsetof(USE_ENUM_STRUCT, Level) == 0, "USE_ENUM_STRUCT.Level");
_Static_assert(__builtin_offsetof(USE_ENUM_STRUCT, UseInfo) == 4, "USE_ENUM_STRUCT.UseInfo");
_Static_assert(sizeof(STAT_WORKSTATION_0) == 216, "size of STAT_WORKSTATION_0");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, StatisticsStartTime) == 0, "STAT_WORKSTATION_0.StatisticsStartTime");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, BytesReceived) == 8, "STAT_WORKSTATION_0.BytesReceived");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmbsReceived) == 16, "STAT_WORKSTATION_0.SmbsReceived");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, PagingReadBytesRequested) == 24, "STAT_WORKSTATION_0.PagingReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NonPagingReadBytesRequested) == 32, "STAT_WORKSTATION_0.NonPagingReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CacheReadBytesRequested) == 40, "STAT_WORKSTATION_0.CacheReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NetworkReadBytesRequested) == 48, "STAT_WORKSTATION_0.NetworkReadBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, BytesTransmitted) == 56, "STAT_WORKSTATION_0.BytesTransmitted");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmbsTransmitted) == 64, "STAT_WORKSTATION_0.SmbsTransmitted");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, PagingWriteBytesRequested) == 72, "STAT_WORKSTATION_0.PagingWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NonPagingWriteBytesRequested) == 80, "STAT_WORKSTATION_0.NonPagingWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CacheWriteBytesRequested) == 88, "STAT_WORKSTATION_0.CacheWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NetworkWriteBytesRequested) == 96, "STAT_WORKSTATION_0.NetworkWriteBytesRequested");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, InitiallyFailedOperations) == 104, "STAT_WORKSTATION_0.InitiallyFailedOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, FailedCompletionOperations) == 108, "STAT_WORKSTATION_0.FailedCompletionOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, ReadOperations) == 112, "STAT_WORKSTATION_0.ReadOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RandomReadOperations) == 116, "STAT_WORKSTATION_0.RandomReadOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, ReadSmbs) == 120, "STAT_WORKSTATION_0.ReadSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, LargeReadSmbs) == 124, "STAT_WORKSTATION_0.LargeReadSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmallReadSmbs) == 128, "STAT_WORKSTATION_0.SmallReadSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, WriteOperations) == 132, "STAT_WORKSTATION_0.WriteOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RandomWriteOperations) == 136, "STAT_WORKSTATION_0.RandomWriteOperations");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, WriteSmbs) == 140, "STAT_WORKSTATION_0.WriteSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, LargeWriteSmbs) == 144, "STAT_WORKSTATION_0.LargeWriteSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, SmallWriteSmbs) == 148, "STAT_WORKSTATION_0.SmallWriteSmbs");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RawReadsDenied) == 152, "STAT_WORKSTATION_0.RawReadsDenied");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, RawWritesDenied) == 156, "STAT_WORKSTATION_0.RawWritesDenied");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, NetworkErrors) == 160, "STAT_WORKSTATION_0.NetworkErrors");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Sessions) == 164, "STAT_WORKSTATION_0.Sessions");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, FailedSessions) == 168, "STAT_WORKSTATION_0.FailedSessions");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Reconnects) == 172, "STAT_WORKSTATION_0.Reconnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CoreConnects) == 176, "STAT_WORKSTATION_0.CoreConnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Lanman20Connects) == 180, "STAT_WORKSTATION_0.Lanman20Connects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, Lanman21Connects) == 184, "STAT_WORKSTATION_0.Lanman21Connects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, LanmanNtConnects) == 188, "STAT_WORKSTATION_0.LanmanNtConnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, ServerDisconnects) == 192, "STAT_WORKSTATION_0.ServerDisconnects");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, HungSessions) == 196, "STAT_WORKSTATION_0.HungSessions");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, UseCount) == 200, "STAT_WORKSTATION_0.UseCount");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, FailedUseCount) == 204, "STAT_WORKSTATION_0.FailedUseCount");
_Static_assert(__builtin_offsetof(STAT_WORKSTATION_0, CurrentCommands) == 208, "STAT_WORKSTATION_0.CurrentCommands");
_Static_assert(sizeof(NETSETUP_JOIN_STATUS) == 4, "size of NETSETUP_JOIN_STATUS");
#endif

/* ------------------------------------------------------------------ interface wkssvc */

#ifndef WKSSVC_CLIENT_ONLY
extern unsigned long WINAPI wkssvc_NetrWkstaGetInfo_s(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned long Level, WKSTA_INFO *WkstaInfo);
extern unsigned long WINAPI wkssvc_NetrWkstaSetInfo_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrWkstaUserEnum_s(WKSSVC_IDENTIFY_HANDLE ServerName, WKSTA_USER_ENUM_STRUCT *UserInfo, unsigned long PreferredMaximumLength, unsigned long *TotalEntries, unsigned long *ResumeHandle);
extern unsigned long WINAPI wkssvc_NetrWkstaUserGetInfo_s(WKSSVC_IMPERSONATE_HANDLE Reserved, unsigned long Level, WKSTA_USER_INFO *UserInfo);
extern unsigned long WINAPI wkssvc_NetrWkstaUserSetInfo_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrWkstaTransportEnum_s(WKSSVC_IDENTIFY_HANDLE ServerName, WKSTA_TRANSPORT_ENUM_STRUCT *TransportInfo, unsigned long PreferredMaximumLength, unsigned long *TotalEntries, unsigned long *ResumeHandle);
extern unsigned long WINAPI wkssvc_NetrWkstaTransportAdd_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrWkstaTransportDel_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrUseAdd_s(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned long Level, USE_INFO *InfoStruct, unsigned long *ParmError);
extern unsigned long WINAPI wkssvc_NetrUseGetInfo_s(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short *UseName, unsigned long Level, USE_INFO *InfoStruct);
extern unsigned long WINAPI wkssvc_NetrUseDel_s(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short *UseName, unsigned long ForceCond);
extern unsigned long WINAPI wkssvc_NetrUseEnum_s(WKSSVC_IDENTIFY_HANDLE ServerName, USE_ENUM_STRUCT *InfoStruct, unsigned long PreferredMaximumLength, unsigned long *TotalEntries, unsigned long *ResumeHandle);
extern unsigned long WINAPI wkssvc_NetrMessageBufferSend_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrWorkstationStatisticsGet_s(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short *ServiceName, unsigned long Level, unsigned long Options, STAT_WORKSTATION_0 **Buffer);
extern unsigned long WINAPI wkssvc_NetrLogonDomainNameAdd_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrLogonDomainNameDel_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrJoinDomain_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrUnjoinDomain_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrRenameMachineInDomain_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrValidateName_s(WKSSVC_IDENTIFY_HANDLE ServerName);
extern unsigned long WINAPI wkssvc_NetrGetJoinInformation_s(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short **NameBuffer, NETSETUP_JOIN_STATUS *BufferType);
#endif
handle_t WINAPI WKSSVC_IDENTIFY_HANDLE_bind(WKSSVC_IDENTIFY_HANDLE);
void WINAPI WKSSVC_IDENTIFY_HANDLE_unbind(WKSSVC_IDENTIFY_HANDLE, handle_t);
handle_t WINAPI WKSSVC_IMPERSONATE_HANDLE_bind(WKSSVC_IMPERSONATE_HANDLE);
void WINAPI WKSSVC_IMPERSONATE_HANDLE_unbind(WKSSVC_IMPERSONATE_HANDLE, handle_t);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char wkssvc_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x11, 0x04, 0x02, 0x00, /* 6: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, /* 10: union WKSTA_INFO */
    0x04, 0x30, 0x64, 0x00, 0x00, 0x00, 0x16, 0x00, 0x65, 0x00, 0x00, 0x00,
    0x2b, 0x00, 0x66, 0x00, 0x00, 0x00, 0x45, 0x00, 0xf6, 0x01, 0x00, 0x00,
    0x61, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x02, 0x00, /* 50: pointer */
    0x1a, 0x03, 0x20, 0x00, 0x00, 0x00, 0x09, 0x00, 0x09, 0x40, 0x36, 0x36, /* 54: struct WKSTA_INFO_100 (BOGUS) */
    0x09, 0x09, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 77: pointer */
    0x1a, 0x03, 0x28, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x09, 0x40, 0x36, 0x36, /* 81: struct WKSTA_INFO_101 (BOGUS) */
    0x09, 0x09, 0x36, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x08, 0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 109: pointer */
    0x1a, 0x03, 0x30, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x09, 0x40, 0x36, 0x36, /* 113: struct WKSTA_INFO_102 (BOGUS) */
    0x09, 0x09, 0x36, 0x09, 0x40, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08,
    0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 143: pointer */
    0x15, 0x03, 0x8c, 0x00, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, /* 147: struct WKSTA_INFO_502 (STRUCT) */
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x5b,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x4c, 0x04, 0x03, /* 187: struct WKSTA_USER_ENUM_STRUCT (BOGUS) */
    0x00, 0x5b,
    0x2b, 0x09, 0x09, 0x00, 0xf8, 0xff, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, /* 201: union WKSTA_USER_ENUM_UNION */
    0x02, 0x30, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x3b, 0x00, 0xff, 0xff,
    0x12, 0x00, 0x02, 0x00, /* 229: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x09, 0x40, 0x36, 0x5b, /* 233: struct WKSTA_USER_INFO_0_CONTAINER (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 249: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x36, 0x5b, 0x12, 0x08, /* 270: struct WKSTA_USER_INFO_0 (BOGUS) */
    0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 284: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x09, 0x40, 0x36, 0x5b, /* 288: struct WKSTA_USER_INFO_1_CONTAINER (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 304: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x20, 0x00, 0x00, 0x00, 0x07, 0x00, 0x36, 0x36, 0x36, 0x36, /* 325: struct WKSTA_USER_INFO_1 (BOGUS) */
    0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25,
    0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x08, 0x09, 0x5c, /* 354: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 358: pointer */
    0x11, 0x04, 0x02, 0x00, /* 362: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, /* 366: union WKSTA_USER_INFO */
    0x03, 0x30, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x4d, 0x04, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x7c, 0xff, /* 400: pointer */
    0x12, 0x00, 0xaf, 0xff, /* 404: pointer */
    0x12, 0x00, 0x02, 0x00, /* 408: pointer */
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x36, 0x5b, 0x12, 0x08, /* 412: struct WKSTA_USER_INFO_1101 (BOGUS) */
    0x25, 0x5c,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x4c, 0x04, 0x03, /* 426: struct WKSTA_TRANSPORT_ENUM_STRUCT (BOGUS) */
    0x00, 0x5b,
    0x2b, 0x09, 0x09, 0x00, 0xf8, 0xff, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, /* 440: union WKSTA_TRANSPORT_ENUM_UNION */
    0x01, 0x30, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0xff, 0xff,
    0x12, 0x00, 0x02, 0x00, /* 462: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x09, 0x40, 0x36, 0x5b, /* 466: struct WKSTA_TRANSPORT_INFO_0_CONTAINER (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 482: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x20, 0x00, 0x00, 0x00, 0x09, 0x00, 0x09, 0x09, 0x36, 0x36, /* 503: struct WKSTA_TRANSPORT_INFO_0 (BOGUS) */
    0x09, 0x40, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x08, 0x09, 0x5c, /* 526: pointer */
    0x11, 0x00, 0x02, 0x00, /* 530: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, /* 534: union USE_INFO */
    0x04, 0x30, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x27, 0x00, 0x02, 0x00, 0x00, 0x00, 0x41, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x56, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x02, 0x00, /* 574: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x36, 0x36, 0x5b, 0x12, /* 578: struct USE_INFO_0 (BOGUS) */
    0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 597: pointer */
    0x1a, 0x03, 0x28, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x36, 0x36, 0x36, 0x09, /* 601: struct USE_INFO_1 (BOGUS) */
    0x09, 0x09, 0x09, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x08, 0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 629: pointer */
    0x1a, 0x03, 0x38, 0x00, 0x00, 0x00, 0x09, 0x00, 0x4c, 0x00, 0xd6, 0xff, /* 633: struct USE_INFO_2 (BOGUS) */
    0x36, 0x36, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 656: pointer */
    0x1a, 0x03, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0xdb, 0xff, /* 660: struct USE_INFO_3 (BOGUS) */
    0x09, 0x40, 0x5b,
    0x12, 0x08, 0x09, 0x5c, /* 675: pointer */
    0x11, 0x08, 0x25, 0x5c, /* 679: pointer */
    0x11, 0x04, 0x02, 0x00, /* 683: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, /* 687: union USE_INFO */
    0x04, 0x30, 0x00, 0x00, 0x00, 0x00, 0x7d, 0xff, 0x01, 0x00, 0x00, 0x00,
    0x8e, 0xff, 0x02, 0x00, 0x00, 0x00, 0xa8, 0xff, 0x03, 0x00, 0x00, 0x00,
    0xbd, 0xff, 0x00, 0x00,
    0x11, 0x08, 0x25, 0x5c, /* 727: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x4c, 0x04, 0x03, /* 731: struct USE_ENUM_STRUCT (BOGUS) */
    0x00, 0x5b,
    0x2b, 0x09, 0x09, 0x00, 0xf8, 0xff, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, /* 745: union USE_ENUM_UNION */
    0x03, 0x30, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x33, 0x00, 0x02, 0x00, 0x00, 0x00, 0x56, 0x00, 0xff, 0xff,
    0x12, 0x00, 0x02, 0x00, /* 779: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x09, 0x40, 0x36, 0x5b, /* 783: struct USE_INFO_0_CONTAINER (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 799: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x11, 0xff, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 820: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x09, 0x40, 0x36, 0x5b, /* 824: struct USE_INFO_1_CONTAINER (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 840: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xff, 0xfe, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 861: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x09, 0x40, 0x36, 0x5b, /* 865: struct USE_INFO_2_CONTAINER (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 881: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xf6, 0xfe, 0x5b,
    0x12, 0x08, 0x09, 0x5c, /* 902: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 906: pointer */
    0x11, 0x14, 0x02, 0x00, /* 910: pointer */
    0x12, 0x00, 0x02, 0x00, /* 914: pointer */
    0x15, 0x07, 0xd8, 0x00, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, /* 918: struct STAT_WORKSTATION_0 (STRUCT) */
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x40, 0x5b,
    0x11, 0x10, 0x02, 0x00, /* 964: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 968: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x11, 0x04, 0x02, 0x00, /* 6: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, /* 10: union WKSTA_INFO */
    0x04, 0x30, 0x64, 0x00, 0x00, 0x00, 0x16, 0x00, 0x65, 0x00, 0x00, 0x00,
    0x35, 0x00, 0x66, 0x00, 0x00, 0x00, 0x5f, 0x00, 0xf6, 0x01, 0x00, 0x00,
    0x8a, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x02, 0x00, /* 50: pointer */
    0x16, 0x03, 0x14, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 54: struct WKSTA_INFO_100 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x5b, 0x09, 0x36, 0x36, 0x09, 0x09, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 87: pointer */
    0x16, 0x03, 0x18, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 91: struct WKSTA_INFO_101 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x46, 0x5c, 0x14, 0x00, 0x14, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x5b, 0x09, 0x36, 0x36, 0x09, 0x09, 0x36, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 135: pointer */
    0x16, 0x03, 0x1c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 139: struct WKSTA_INFO_102 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x46, 0x5c, 0x14, 0x00, 0x14, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x5b, 0x09, 0x36, 0x36, 0x09, 0x09, 0x36, 0x09, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 184: pointer */
    0x15, 0x03, 0x8c, 0x00, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, /* 188: struct WKSTA_INFO_502 (STRUCT) */
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x5b,
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x4c, 0x00, 0x03, /* 228: struct WKSTA_USER_ENUM_STRUCT (BOGUS) */
    0x00, 0x5b,
    0x2b, 0x09, 0x09, 0x00, 0xfc, 0xff, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, /* 242: union WKSTA_USER_ENUM_UNION */
    0x02, 0x30, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x51, 0x00, 0xff, 0xff,
    0x12, 0x00, 0x02, 0x00, /* 270: pointer */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 274: struct WKSTA_USER_INFO_0_CONTAINER (PSTRUCT) */
    0x12, 0x00, 0x06, 0x00, 0x5b, 0x09, 0x36, 0x5b,
    0x1b, 0x03, 0x04, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 294: array */
    0x48, 0x49, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x04, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 328: struct WKSTA_USER_INFO_0 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x36, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 347: pointer */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 351: struct WKSTA_USER_INFO_1_CONTAINER (PSTRUCT) */
    0x12, 0x00, 0x06, 0x00, 0x5b, 0x09, 0x36, 0x5b,
    0x1b, 0x03, 0x10, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 371: array */
    0x48, 0x49, 0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x08, 0x00, 0x08, 0x00, 0x12, 0x08, 0x25, 0x5c, 0x0c, 0x00, 0x0c, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x10, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 429: struct WKSTA_USER_INFO_1 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x46, 0x5c, 0x0c, 0x00, 0x0c, 0x00, 0x12, 0x08, 0x25, 0x5c, 0x5b, 0x36,
    0x36, 0x36, 0x36, 0x5b,
    0x12, 0x08, 0x09, 0x5c, /* 481: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 485: pointer */
    0x11, 0x04, 0x02, 0x00, /* 489: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, /* 493: union WKSTA_USER_INFO */
    0x03, 0x30, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x4d, 0x04, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x37, 0xff, /* 527: pointer */
    0x12, 0x00, 0x98, 0xff, /* 531: pointer */
    0x12, 0x00, 0x02, 0x00, /* 535: pointer */
    0x16, 0x03, 0x04, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 539: struct WKSTA_USER_INFO_1101 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x36, 0x5b,
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x4c, 0x00, 0x03, /* 558: struct WKSTA_TRANSPORT_ENUM_STRUCT (BOGUS) */
    0x00, 0x5b,
    0x2b, 0x09, 0x09, 0x00, 0xfc, 0xff, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, /* 572: union WKSTA_TRANSPORT_ENUM_UNION */
    0x01, 0x30, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0xff, 0xff,
    0x12, 0x00, 0x02, 0x00, /* 594: pointer */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 598: struct WKSTA_TRANSPORT_INFO_0_CONTAINER (PSTRUCT) */
    0x12, 0x00, 0x06, 0x00, 0x5b, 0x09, 0x36, 0x5b,
    0x1b, 0x03, 0x14, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 618: array */
    0x48, 0x49, 0x14, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, 0x08, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x0c, 0x00, 0x0c, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x14, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 660: struct WKSTA_TRANSPORT_INFO_0 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x0c, 0x00, 0x0c, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x5b, 0x09, 0x09, 0x36, 0x36, 0x09, 0x5b,
    0x12, 0x08, 0x09, 0x5c, /* 693: pointer */
    0x11, 0x00, 0x02, 0x00, /* 697: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, /* 701: union USE_INFO */
    0x04, 0x30, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x32, 0x00, 0x02, 0x00, 0x00, 0x00, 0x5d, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x72, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x02, 0x00, /* 741: pointer */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 745: struct USE_INFO_0 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x5b, 0x36, 0x36, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 775: pointer */
    0x16, 0x03, 0x1c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 779: struct USE_INFO_1 (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x5b, 0x36, 0x36, 0x36, 0x09, 0x09, 0x09, 0x09, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 824: pointer */
    0x1a, 0x03, 0x24, 0x00, 0x00, 0x00, 0x09, 0x00, 0x4c, 0x00, 0xc5, 0xff, /* 828: struct USE_INFO_2 (BOGUS) */
    0x36, 0x36, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 851: pointer */
    0x1a, 0x03, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0xdb, 0xff, /* 855: struct USE_INFO_3 (BOGUS) */
    0x09, 0x5b,
    0x12, 0x08, 0x09, 0x5c, /* 869: pointer */
    0x11, 0x08, 0x25, 0x5c, /* 873: pointer */
    0x11, 0x04, 0x02, 0x00, /* 877: pointer */
    0x2b, 0x09, 0x29, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, /* 881: union USE_INFO */
    0x04, 0x30, 0x00, 0x00, 0x00, 0x00, 0x62, 0xff, 0x01, 0x00, 0x00, 0x00,
    0x7e, 0xff, 0x02, 0x00, 0x00, 0x00, 0xa9, 0xff, 0x03, 0x00, 0x00, 0x00,
    0xbe, 0xff, 0x00, 0x00,
    0x11, 0x08, 0x25, 0x5c, /* 921: pointer */
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x4c, 0x00, 0x03, /* 925: struct USE_ENUM_STRUCT (BOGUS) */
    0x00, 0x5b,
    0x2b, 0x09, 0x09, 0x00, 0xfc, 0xff, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, /* 939: union USE_ENUM_UNION */
    0x03, 0x30, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x4c, 0x00, 0x02, 0x00, 0x00, 0x00, 0x90, 0x00, 0xff, 0xff,
    0x12, 0x00, 0x02, 0x00, /* 973: pointer */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 977: struct USE_INFO_0_CONTAINER (PSTRUCT) */
    0x12, 0x00, 0x06, 0x00, 0x5b, 0x09, 0x36, 0x5b,
    0x1b, 0x03, 0x08, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 997: array */
    0x48, 0x49, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x5b, 0x4c, 0x00, 0xdd, 0xfe, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 1039: pointer */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 1043: struct USE_INFO_1_CONTAINER (PSTRUCT) */
    0x12, 0x00, 0x06, 0x00, 0x5b, 0x09, 0x36, 0x5b,
    0x1b, 0x03, 0x1c, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 1063: array */
    0x48, 0x49, 0x1c, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x08, 0x00, 0x08, 0x00, 0x12, 0x08, 0x25, 0x5c, 0x5b, 0x4c, 0x00, 0xb5,
    0xfe, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 1113: pointer */
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x05, 0x00, 0x09, 0x36, 0x5b, 0x12, /* 1117: struct USE_INFO_2_CONTAINER (BOGUS) */
    0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 1132: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xbe, 0xfe, 0x5b,
    0x12, 0x08, 0x09, 0x5c, /* 1153: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 1157: pointer */
    0x11, 0x14, 0x02, 0x00, /* 1161: pointer */
    0x12, 0x00, 0x02, 0x00, /* 1165: pointer */
    0x15, 0x07, 0xd8, 0x00, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, /* 1169: struct STAT_WORKSTATION_0 (STRUCT) */
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x40, 0x5b,
    0x11, 0x10, 0x02, 0x00, /* 1215: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 1219: pointer */
#endif
};

static const unsigned char wkssvc_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 2: wkssvc.NetrWkstaGetInfo */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x48, 0x00,
    0x08, 0x00, 0x09, 0x5c, 0x13, 0x00, 0x10, 0x00, 0x06, 0x00, 0x70, 0x00,
    0x18, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 54: wkssvc.NetrWkstaSetInfo */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x30, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 94: wkssvc.NetrWkstaUserEnum */
    0x04, 0x00, 0x08, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x01,
    0x08, 0x00, 0xbb, 0x00, 0x48, 0x00, 0x10, 0x00, 0x09, 0x5c, 0x50, 0x21,
    0x18, 0x00, 0x09, 0x5c, 0x1b, 0x00, 0x20, 0x00, 0x62, 0x01, 0x70, 0x00,
    0x28, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x20, 0x00, 0x31, 0x08, 0x00, 0x00, 0x01, 0x5c, /* 158: wkssvc.NetrWkstaUserGetInfo */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x66, 0x01, 0x48, 0x00,
    0x08, 0x00, 0x09, 0x5c, 0x13, 0x00, 0x10, 0x00, 0x6a, 0x01, 0x70, 0x00,
    0x18, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 210: wkssvc.NetrWkstaUserSetInfo */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x30, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 250: wkssvc.NetrWkstaTransportEnum */
    0x04, 0x00, 0x08, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x01,
    0x08, 0x00, 0xaa, 0x01, 0x48, 0x00, 0x10, 0x00, 0x09, 0x5c, 0x50, 0x21,
    0x18, 0x00, 0x09, 0x5c, 0x1b, 0x00, 0x20, 0x00, 0x0e, 0x02, 0x70, 0x00,
    0x28, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 314: wkssvc.NetrWkstaTransportAdd */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 354: wkssvc.NetrWkstaTransportDel */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x28, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 394: wkssvc.NetrUseAdd */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x05, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x48, 0x00,
    0x08, 0x00, 0x09, 0x5c, 0x0b, 0x00, 0x10, 0x00, 0x12, 0x02, 0x1b, 0x00,
    0x18, 0x00, 0xa3, 0x02, 0x70, 0x00, 0x20, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x28, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 452: wkssvc.NetrUseGetInfo */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x05, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x00,
    0x08, 0x00, 0xa7, 0x02, 0x48, 0x00, 0x10, 0x00, 0x09, 0x5c, 0x13, 0x00,
    0x18, 0x00, 0xab, 0x02, 0x70, 0x00, 0x20, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x20, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 510: wkssvc.NetrUseDel */
    0x04, 0x00, 0x04, 0x00, 0x46, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x00,
    0x08, 0x00, 0xd7, 0x02, 0x48, 0x00, 0x10, 0x00, 0x09, 0x5c, 0x70, 0x00,
    0x18, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x30, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 562: wkssvc.NetrUseEnum */
    0x04, 0x00, 0x08, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x01,
    0x08, 0x00, 0xdb, 0x02, 0x48, 0x00, 0x10, 0x00, 0x09, 0x5c, 0x50, 0x21,
    0x18, 0x00, 0x09, 0x5c, 0x1b, 0x00, 0x20, 0x00, 0x86, 0x03, 0x70, 0x00,
    0x28, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 626: wkssvc.NetrMessageBufferSend */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x30, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 666: wkssvc.NetrWorkstationStatisticsGet */
    0x08, 0x00, 0x04, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x00,
    0x08, 0x00, 0x8a, 0x03, 0x48, 0x00, 0x10, 0x00, 0x09, 0x5c, 0x48, 0x00,
    0x18, 0x00, 0x09, 0x5c, 0x13, 0x20, 0x20, 0x00, 0x8e, 0x03, 0x70, 0x00,
    0x28, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 730: wkssvc.NetrLogonDomainNameAdd */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 770: wkssvc.NetrLogonDomainNameDel */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 810: wkssvc.NetrJoinDomain */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 850: wkssvc.NetrUnjoinDomain */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 890: wkssvc.NetrRenameMachineInDomain */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x10, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 930: wkssvc.NetrValidateName */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x20, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 970: wkssvc.NetrGetJoinInformation */
    0x00, 0x00, 0x08, 0x00, 0x47, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x00,
    0x08, 0x00, 0xc4, 0x03, 0x50, 0x21, 0x10, 0x00, 0x0d, 0x5c, 0x70, 0x00,
    0x18, 0x00, 0x09, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x10, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 2: wkssvc.NetrWkstaGetInfo */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x48, 0x00, 0x04, 0x00,
    0x09, 0x5c, 0x13, 0x00, 0x08, 0x00, 0x06, 0x00, 0x70, 0x00, 0x0c, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 52: wkssvc.NetrWkstaSetInfo */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x18, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 90: wkssvc.NetrWkstaUserEnum */
    0x04, 0x00, 0x08, 0x00, 0x47, 0x06, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x01, 0x04, 0x00,
    0xe4, 0x00, 0x48, 0x00, 0x08, 0x00, 0x09, 0x5c, 0x50, 0x21, 0x0c, 0x00,
    0x09, 0x5c, 0x1b, 0x00, 0x10, 0x00, 0xe1, 0x01, 0x70, 0x00, 0x14, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x10, 0x00, 0x31, 0x04, 0x00, 0x00, 0x01, 0x5c, /* 152: wkssvc.NetrWkstaUserGetInfo */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0xe5, 0x01, 0x48, 0x00, 0x04, 0x00,
    0x09, 0x5c, 0x13, 0x00, 0x08, 0x00, 0xe9, 0x01, 0x70, 0x00, 0x0c, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 202: wkssvc.NetrWkstaUserSetInfo */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x18, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 240: wkssvc.NetrWkstaTransportEnum */
    0x04, 0x00, 0x08, 0x00, 0x47, 0x06, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x01, 0x04, 0x00,
    0x2e, 0x02, 0x48, 0x00, 0x08, 0x00, 0x09, 0x5c, 0x50, 0x21, 0x0c, 0x00,
    0x09, 0x5c, 0x1b, 0x00, 0x10, 0x00, 0xb5, 0x02, 0x70, 0x00, 0x14, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 302: wkssvc.NetrWkstaTransportAdd */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 340: wkssvc.NetrWkstaTransportDel */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x14, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 378: wkssvc.NetrUseAdd */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x05, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x48, 0x00, 0x04, 0x00,
    0x09, 0x5c, 0x0b, 0x00, 0x08, 0x00, 0xb9, 0x02, 0x1b, 0x00, 0x0c, 0x00,
    0x65, 0x03, 0x70, 0x00, 0x10, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x14, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 434: wkssvc.NetrUseGetInfo */
    0x04, 0x00, 0x04, 0x00, 0x47, 0x05, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x04, 0x00,
    0x69, 0x03, 0x48, 0x00, 0x08, 0x00, 0x09, 0x5c, 0x13, 0x00, 0x0c, 0x00,
    0x6d, 0x03, 0x70, 0x00, 0x10, 0x00, 0x09, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x10, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 490: wkssvc.NetrUseDel */
    0x04, 0x00, 0x04, 0x00, 0x46, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x04, 0x00,
    0x99, 0x03, 0x48, 0x00, 0x08, 0x00, 0x09, 0x5c, 0x70, 0x00, 0x0c, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x18, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 540: wkssvc.NetrUseEnum */
    0x04, 0x00, 0x08, 0x00, 0x47, 0x06, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x01, 0x04, 0x00,
    0x9d, 0x03, 0x48, 0x00, 0x08, 0x00, 0x09, 0x5c, 0x50, 0x21, 0x0c, 0x00,
    0x09, 0x5c, 0x1b, 0x00, 0x10, 0x00, 0x81, 0x04, 0x70, 0x00, 0x14, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 602: wkssvc.NetrMessageBufferSend */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x18, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 640: wkssvc.NetrWorkstationStatisticsGet */
    0x08, 0x00, 0x04, 0x00, 0x47, 0x06, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x04, 0x00,
    0x85, 0x04, 0x48, 0x00, 0x08, 0x00, 0x09, 0x5c, 0x48, 0x00, 0x0c, 0x00,
    0x09, 0x5c, 0x13, 0x20, 0x10, 0x00, 0x89, 0x04, 0x70, 0x00, 0x14, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 702: wkssvc.NetrLogonDomainNameAdd */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 740: wkssvc.NetrLogonDomainNameDel */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 778: wkssvc.NetrJoinDomain */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 816: wkssvc.NetrUnjoinDomain */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 854: wkssvc.NetrRenameMachineInDomain */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x08, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 892: wkssvc.NetrValidateName */
    0x00, 0x00, 0x04, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x09, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x10, 0x00, 0x31, 0x04, 0x00, 0x00, 0x00, 0x5c, /* 930: wkssvc.NetrGetJoinInformation */
    0x00, 0x00, 0x08, 0x00, 0x47, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1b, 0x00, 0x04, 0x00,
    0xbf, 0x04, 0x50, 0x21, 0x08, 0x00, 0x0d, 0x5c, 0x70, 0x00, 0x0c, 0x00,
    0x09, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short wkssvc_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 54, 94, 158, 210, 250, 314, 354, 394, 452, 510, 562, 626, 666, 730, 770, 810, 850, 890, 930, 970
#else
    2, 52, 90, 152, 202, 240, 302, 340, 378, 434, 490, 540, 602, 640, 702, 740, 778, 816, 854, 892, 930
#endif
};

static const GENERIC_BINDING_ROUTINE_PAIR wkssvc_BindingRoutines[] = {
    { (GENERIC_BINDING_ROUTINE)(void *)WKSSVC_IDENTIFY_HANDLE_bind, (GENERIC_UNBIND_ROUTINE)(void *)WKSSVC_IDENTIFY_HANDLE_unbind },
    { (GENERIC_BINDING_ROUTINE)(void *)WKSSVC_IMPERSONATE_HANDLE_bind, (GENERIC_UNBIND_ROUTINE)(void *)WKSSVC_IMPERSONATE_HANDLE_unbind }
};

static RPC_CLIENT_INTERFACE wkssvc_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x6bffd098, 0xa112, 0x3610, {0x98, 0x33, 0x46, 0xc3, 0xf8, 0x7e, 0x34, 0x5a}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE wkssvc_c_ifspec = (RPC_IF_HANDLE)&wkssvc_c_ifspec_data;

static MIDL_STUB_DESC wkssvc_StubDesc = {
    .RpcInterfaceInformation = &wkssvc_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .aGenericBindingRoutinePairs = wkssvc_BindingRoutines,
    .pFormatTypes = wkssvc_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef WKSSVC_CLIENT_ONLY
static const SERVER_ROUTINE wkssvc_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaGetInfo_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaSetInfo_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaUserEnum_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaUserGetInfo_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaUserSetInfo_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaTransportEnum_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaTransportAdd_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWkstaTransportDel_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrUseAdd_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrUseGetInfo_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrUseDel_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrUseEnum_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrMessageBufferSend_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrWorkstationStatisticsGet_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrLogonDomainNameAdd_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrLogonDomainNameDel_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrJoinDomain_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrUnjoinDomain_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrRenameMachineInDomain_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrValidateName_s,
    (SERVER_ROUTINE)(void *)wkssvc_NetrGetJoinInformation_s
};

int (*wkssvc_hook)(PRPC_MESSAGE);

static void __RPC_STUB wkssvc_dispatch_0(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_1(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_2(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_3(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_4(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_5(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_6(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_7(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_8(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_9(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_10(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_11(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_12(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_13(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_14(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_15(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_16(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_17(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_18(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_19(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB wkssvc_dispatch_20(PRPC_MESSAGE m)
{
    if (wkssvc_hook && wkssvc_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION wkssvc_DispatchFunctions[] = {
wkssvc_dispatch_0, wkssvc_dispatch_1, wkssvc_dispatch_2, wkssvc_dispatch_3, wkssvc_dispatch_4, wkssvc_dispatch_5, wkssvc_dispatch_6, wkssvc_dispatch_7, wkssvc_dispatch_8, wkssvc_dispatch_9, wkssvc_dispatch_10, wkssvc_dispatch_11, wkssvc_dispatch_12, wkssvc_dispatch_13, wkssvc_dispatch_14, wkssvc_dispatch_15, wkssvc_dispatch_16, wkssvc_dispatch_17, wkssvc_dispatch_18, wkssvc_dispatch_19, wkssvc_dispatch_20
};
static RPC_DISPATCH_TABLE wkssvc_DispatchTable = { 21, wkssvc_DispatchFunctions, 0 };
static MIDL_SERVER_INFO wkssvc_ServerInfo = {
    .pStubDesc = &wkssvc_StubDesc,
    .DispatchTable = wkssvc_ServerRoutineTable,
    .ProcString = wkssvc_ProcFormat,
    .FmtStringOffset = wkssvc_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE wkssvc_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x6bffd098, 0xa112, 0x3610, {0x98, 0x33, 0x46, 0xc3, 0xf8, 0x7e, 0x34, 0x5a}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &wkssvc_DispatchTable, 0, 0, 0, &wkssvc_ServerInfo, 0 };
RPC_IF_HANDLE wkssvc_s_ifspec = (RPC_IF_HANDLE)&wkssvc_s_ifspec_data;
#endif

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaGetInfo(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned long Level, WKSTA_INFO *WkstaInfo)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[0], ServerName, Level, WkstaInfo).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        unsigned long Level;
        WKSTA_INFO *WkstaInfo;
    } frame = { ServerName, Level, WkstaInfo };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaSetInfo(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[1], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaUserEnum(WKSSVC_IDENTIFY_HANDLE ServerName, WKSTA_USER_ENUM_STRUCT *UserInfo, unsigned long PreferredMaximumLength, unsigned long *TotalEntries, unsigned long *ResumeHandle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[2], ServerName, UserInfo, PreferredMaximumLength, TotalEntries, ResumeHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        WKSTA_USER_ENUM_STRUCT *UserInfo;
        unsigned long PreferredMaximumLength;
        unsigned long *TotalEntries;
        unsigned long *ResumeHandle;
    } frame = { ServerName, UserInfo, PreferredMaximumLength, TotalEntries, ResumeHandle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaUserGetInfo(WKSSVC_IMPERSONATE_HANDLE Reserved, unsigned long Level, WKSTA_USER_INFO *UserInfo)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[3], Reserved, Level, UserInfo).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IMPERSONATE_HANDLE Reserved;
        unsigned long Level;
        WKSTA_USER_INFO *UserInfo;
    } frame = { Reserved, Level, UserInfo };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaUserSetInfo(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[4], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaTransportEnum(WKSSVC_IDENTIFY_HANDLE ServerName, WKSTA_TRANSPORT_ENUM_STRUCT *TransportInfo, unsigned long PreferredMaximumLength, unsigned long *TotalEntries, unsigned long *ResumeHandle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[5], ServerName, TransportInfo, PreferredMaximumLength, TotalEntries, ResumeHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        WKSTA_TRANSPORT_ENUM_STRUCT *TransportInfo;
        unsigned long PreferredMaximumLength;
        unsigned long *TotalEntries;
        unsigned long *ResumeHandle;
    } frame = { ServerName, TransportInfo, PreferredMaximumLength, TotalEntries, ResumeHandle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaTransportAdd(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[6], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWkstaTransportDel(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[7], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[7], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrUseAdd(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned long Level, USE_INFO *InfoStruct, unsigned long *ParmError)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[8], ServerName, Level, InfoStruct, ParmError).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        unsigned long Level;
        USE_INFO *InfoStruct;
        unsigned long *ParmError;
    } frame = { ServerName, Level, InfoStruct, ParmError };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[8], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrUseGetInfo(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short *UseName, unsigned long Level, USE_INFO *InfoStruct)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[9], ServerName, UseName, Level, InfoStruct).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        unsigned short *UseName;
        unsigned long Level;
        USE_INFO *InfoStruct;
    } frame = { ServerName, UseName, Level, InfoStruct };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[9], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrUseDel(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short *UseName, unsigned long ForceCond)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[10], ServerName, UseName, ForceCond).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        unsigned short *UseName;
        unsigned long ForceCond;
    } frame = { ServerName, UseName, ForceCond };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[10], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrUseEnum(WKSSVC_IDENTIFY_HANDLE ServerName, USE_ENUM_STRUCT *InfoStruct, unsigned long PreferredMaximumLength, unsigned long *TotalEntries, unsigned long *ResumeHandle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[11], ServerName, InfoStruct, PreferredMaximumLength, TotalEntries, ResumeHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        USE_ENUM_STRUCT *InfoStruct;
        unsigned long PreferredMaximumLength;
        unsigned long *TotalEntries;
        unsigned long *ResumeHandle;
    } frame = { ServerName, InfoStruct, PreferredMaximumLength, TotalEntries, ResumeHandle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[11], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrMessageBufferSend(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[12], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[12], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrWorkstationStatisticsGet(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short *ServiceName, unsigned long Level, unsigned long Options, STAT_WORKSTATION_0 **Buffer)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[13], ServerName, ServiceName, Level, Options, Buffer).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        unsigned short *ServiceName;
        unsigned long Level;
        unsigned long Options;
        STAT_WORKSTATION_0 **Buffer;
    } frame = { ServerName, ServiceName, Level, Options, Buffer };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[13], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrLogonDomainNameAdd(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[14], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[14], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrLogonDomainNameDel(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[15], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[15], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrJoinDomain(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[16], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[16], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrUnjoinDomain(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[17], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[17], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrRenameMachineInDomain(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[18], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[18], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrValidateName(WKSSVC_IDENTIFY_HANDLE ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[19], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[19], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI wkssvc_NetrGetJoinInformation(WKSSVC_IDENTIFY_HANDLE ServerName, unsigned short **NameBuffer, NETSETUP_JOIN_STATUS *BufferType)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[20], ServerName, NameBuffer, BufferType).Simple;
#else
#pragma pack(push, 4)
    struct {
        WKSSVC_IDENTIFY_HANDLE ServerName;
        unsigned short **NameBuffer;
        NETSETUP_JOIN_STATUS *BufferType;
    } frame = { ServerName, NameBuffer, BufferType };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&wkssvc_StubDesc, wkssvc_ProcFormat + wkssvc_FormatStringOffsetTable[20], (unsigned char *)&frame).Simple;
#endif
}

#endif
