/* types and structures of the jet api. the constants are in jet_const.h. */
#pragma once
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

typedef LONG JET_ERR;
typedef ULONG_PTR JET_API_PTR, JET_INSTANCE, JET_SESID, JET_TABLEID, JET_LS;
typedef ULONG JET_DBID, JET_GRBIT, JET_COLUMNID, JET_COLTYP, JET_OBJTYP, JET_SNP, JET_SNT, JET_CBTYP;
typedef double JET_DATESERIAL;

#include "jet_const.h"

typedef struct {
    USHORT ib, isec;
    LONG lGeneration;
} JET_LGPOS;

typedef struct {
    CHAR bSeconds, bMinutes, bHours, bDay, bMonth, bYear, bFiller1, bFiller2;
} JET_LOGTIME;

typedef struct {
    ULONG ulRandom;
    JET_LOGTIME logtimeCreate;
    CHAR szComputerName[JET_MAX_COMPUTERNAME_LENGTH + 1];
} JET_SIGNATURE;

typedef struct {
    ULONG lcid, dwMapFlags;
} JET_UNICODEINDEX;

typedef struct {
    LPCWSTR szLocaleName;
    ULONG dwMapFlags;
} JET_UNICODEINDEX2;

typedef struct {
    ULONG chLengthMin, chLengthMax, chToIndexMax, cchIncrement, ichStart;
} JET_TUPLELIMITS;

typedef struct {
    JET_LGPOS lgposMark;
    JET_LOGTIME logtimeMark;
    ULONG genLow, genHigh;
} JET_BKINFO;

typedef struct {
    ULONG cbStruct;
    JET_COLUMNID columnid;
    JET_COLTYP coltyp;
    USHORT wCountry, langid, cp, wCollate;
    ULONG cbMax;
    JET_GRBIT grbit;
} JET_COLUMNDEF;

typedef struct {
    ULONG cbStruct;
    JET_COLUMNID columnid;
    JET_COLTYP coltyp;
    USHORT wCountry, langid, cp, wFiller;
    ULONG cbMax;
    JET_GRBIT grbit;
    WCHAR szBaseTableName[256], szBaseColumnName[256];
} JET_COLUMNBASE_W;

typedef struct {
    ULONG cbStruct;
    JET_COLUMNID columnid;
    JET_COLTYP coltyp;
    USHORT wCountry, langid, cp, wFiller;
    ULONG cbMax;
    JET_GRBIT grbit;
    CHAR szBaseTableName[256], szBaseColumnName[256];
} JET_COLUMNBASE_A;

typedef struct {
    ULONG cbStruct;
    void *szColumnName;
    JET_COLTYP coltyp;
    ULONG cbMax;
    JET_GRBIT grbit;
    void *pvDefault;
    ULONG cbDefault, cp;
    JET_COLUMNID columnid;
    JET_ERR err;
} JET_COLUMNCREATE;

typedef struct {
    ULONG cbStruct;
    JET_TABLEID tableid;
    ULONG cRecord;
    JET_COLUMNID columnidPresentationOrder, columnidcolumnname, columnidcolumnid, columnidcoltyp, columnidCountry,
        columnidLangid, columnidCp, columnidCollate, columnidcbMax, columnidgrbit, columnidDefault,
        columnidBaseTableName, columnidBaseColumnName, columnidDefinitionName;
} JET_COLUMNLIST;

typedef struct {
    ULONG cbStruct;
    void *szColumnName;
    JET_GRBIT grbit;
} JET_CONDITIONALCOLUMN;

typedef struct {
    ULONG cbStruct;
    JET_TABLEID tableid;
    ULONG cRecord;
    JET_COLUMNID columnidindexname, columnidgrbitIndex, columnidcKey, columnidcEntry, columnidcPage, columnidcColumn,
        columnidiColumn, columnidcolumnid, columnidcoltyp, columnidCountry, columnidLangid, columnidCp, columnidCollate,
        columnidgrbitColumn, columnidcolumnname, columnidLCMapFlags;
} JET_INDEXLIST;

typedef struct {
    ULONG cbStruct;
    JET_TABLEID tableid;
    ULONG cRecord;
    JET_COLUMNID columnidcontainername, columnidobjectname, columnidobjtyp, columniddtCreate, columniddtUpdate,
        columnidgrbit, columnidflags, columnidcRecord, columnidcPage;
} JET_OBJECTLIST;

typedef struct {
    ULONG cbStruct;
    JET_OBJTYP objtyp;
    JET_DATESERIAL dtCreate, dtUpdate;
    JET_GRBIT grbit;
    ULONG flags, cRecord, cPage;
} JET_OBJECTINFO;

typedef struct {
    ULONG cbStruct, centriesLT, centriesInRange, centriesTotal;
} JET_RECPOS;

typedef struct {
    ULONG64 cbData, cbLongValueData, cbOverhead, cbLongValueOverhead, cNonTaggedColumns, cTaggedColumns, cLongValues,
        cMultiValues;
} JET_RECSIZE;

typedef struct {
    ULONG cbStruct, ibLongValue, itagSequence;
    JET_COLUMNID columnidNextTagged;
} JET_RETINFO;

typedef struct {
    JET_COLUMNID columnid;
    void *pvData;
    ULONG cbData, cbActual;
    JET_GRBIT grbit;
    ULONG ibLongValue, itagSequence;
    JET_COLUMNID columnidNextTagged;
    JET_ERR err;
} JET_RETRIEVECOLUMN;

typedef struct {
    JET_COLUMNID columnid;
    const void *pvData;
    ULONG cbData;
    JET_GRBIT grbit;
    ULONG ibLongValue, itagSequence;
    JET_ERR err;
} JET_SETCOLUMN;

typedef struct {
    ULONG cbStruct, ibLongValue, itagSequence;
} JET_SETINFO;

typedef struct {
    ULONG itagSequence;
    JET_ERR err;
    ULONG cbData;
    void *pvData;
} JET_ENUMCOLUMNVALUE;

typedef struct {
    JET_COLUMNID columnid;
    JET_ERR err;
    union {
        struct {
            ULONG cEnumColumnValue;
            JET_ENUMCOLUMNVALUE *rgEnumColumnValue;
        };
        struct {
            ULONG cbData;
            void *pvData;
        };
    };
} JET_ENUMCOLUMN;

typedef struct {
    JET_COLUMNID columnid;
    ULONG ctagSequence;
    ULONG *rgtagSequence;
} JET_ENUMCOLUMNID;

typedef struct {
    ULONG paramid;
    JET_API_PTR lParam;
    const void *sz;
    JET_ERR err;
} JET_SETSYSPARAM;

typedef struct {
    ULONG cbStruct;
    void *szIndexName, *szKey;
    ULONG cbKey;
    JET_GRBIT grbit;
    ULONG ulDensity;
    union {
        ULONG lcid;
        void *pidxunicode;
    };
    union {
        ULONG cbVarSegMac;
        JET_TUPLELIMITS *ptuplelimits;
    };
    JET_CONDITIONALCOLUMN *rgconditionalcolumn;
    ULONG cConditionalColumn;
    JET_ERR err;
    ULONG cbKeyMost;
} JET_INDEXCREATE;

typedef struct {
    JET_INDEXCREATE v1;
    void *pSpacehints;
} JET_INDEXCREATE2;

typedef struct {
    ULONG cbStruct, cPageReferenced, cPageRead, cPagePreread, cPageDirtied, cPageRedirtied, cLogRecord, cbLogRecord;
} JET_THREADSTATS;

typedef struct {
    ULONG cbStruct;
    const JET_COLUMNDEF *prgcolumndef;
    ULONG ccolumn;
    void *pidxunicode;
    JET_GRBIT grbit;
    JET_COLUMNID *prgcolumnid;
    ULONG cbKeyMost, cbVarSegMac;
    JET_TABLEID tableid;
} JET_OPENTEMPORARYTABLE;

typedef struct {
    ULONG ulVersion, ulUpdate;
    JET_SIGNATURE signDb;
    ULONG dbstate;
    JET_LGPOS lgposConsistent;
    JET_LOGTIME logtimeConsistent, logtimeAttach;
    JET_LGPOS lgposAttach;
    JET_LOGTIME logtimeDetach;
    JET_LGPOS lgposDetach;
    JET_SIGNATURE signLog;
    JET_BKINFO bkinfoFullPrev, bkinfoIncPrev, bkinfoFullCur;
    ULONG fShadowingDisabled, fUpgradeDb, dwMajorVersion, dwMinorVersion, dwBuildNumber;
    LONG lSPNumber;
    ULONG cbPageSize;
} JET_DBINFOMISC;

typedef struct {
    ULONG cbStruct;
    void *szTableName, *szTemplateTableName;
    ULONG ulPages, ulDensity;
    void *rgcolumncreate;
    ULONG cColumns;
    void *rgindexcreate;
    ULONG cIndexes;
    JET_GRBIT grbit;
    JET_TABLEID tableid;
    ULONG cCreated;
} JET_TABLECREATE;

typedef struct {
    ULONG cbStruct;
    void *szTableName, *szTemplateTableName;
    ULONG ulPages, ulDensity;
    void *rgcolumncreate;
    ULONG cColumns;
    void *rgindexcreate;
    ULONG cIndexes;
    void *szCallback;
    JET_CBTYP cbtyp;
    JET_GRBIT grbit;
    JET_TABLEID tableid;
    ULONG cCreated;
} JET_TABLECREATE2;

typedef struct {
    ULONG cbStruct;
    void *szTableName, *szTemplateTableName;
    ULONG ulPages, ulDensity;
    void *rgcolumncreate;
    ULONG cColumns;
    void *rgindexcreate;
    ULONG cIndexes;
    void *szCallback;
    JET_CBTYP cbtyp;
    JET_GRBIT grbit;
    void *pSeqSpacehints, *pLVSpacehints;
    ULONG cbSeparateLV;
    JET_TABLEID tableid;
    ULONG cCreated;
} JET_TABLECREATE3;

typedef struct {
    ULONG paramid;
    JET_API_PTR value;
} JET_SETDBPARAM;

typedef struct {
    ULONG64 commitid;
} JET_COMMIT_ID;

typedef ULONG_PTR JET_HANDLE;

typedef struct {
    ULONG cbStruct;
    ULONG cbInitial, cbMaxPerExtent;
} JET_DUMMYSTRUCT;

typedef struct {
    ULONG cbStruct;
    ULONG ulInitialDensity;
    ULONG cbInitial, grbit, ulMaintDensity, ulGrowth;
    ULONG cbMinExtent, cbMaxExtent;
} JET_SPACEHINTS;

typedef struct {
    JET_INSTANCE hInstanceId;
    void *szInstanceName;
    JET_API_PTR cDatabases;
    void **szDatabaseFileName, **szDatabaseDisplayName, **szDatabaseSLVFileName;
} JET_INSTANCE_INFO;

typedef struct {
    ULONG cbStruct;
    void *rgrstmap;
    LONG crstmap;
    JET_LGPOS lgposStop;
    JET_LOGTIME logtimeStop;
    void *pfnStatus;
} JET_RSTINFO;

typedef struct {
    ULONG cbStruct, cbFilesizeLow, cbFilesizeHigh, cbFreeSpaceRequiredLow, cbFreeSpaceRequiredHigh, csecToUpgrade,
        ulFlags;
} JET_DBINFOUPGRADE;

