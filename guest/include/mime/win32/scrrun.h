/*
 * the scripting runtime, scrrun.dll: the dual interfaces of the FileSystemObject, its drives, folders, files and text
 * streams, and of the Dictionary. interfaces are C structs whose first member points to a vtable, laid out as the type
 * library of Windows has them.
 */
#ifndef MIME_WIN32_SCRRUN_H
#define MIME_WIN32_SCRRUN_H

#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>

MIME_DEFINE_GUID(LIBID_Scripting, 0x420B2830, 0xE718, 0x11CF, 0x89, 0x3D, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x28);
MIME_DEFINE_GUID(CLSID_Dictionary, 0xEE09B103, 0x97E0, 0x11CF, 0x97, 0x8F, 0x00, 0xA0, 0x24, 0x63, 0xE0, 0x6F);
MIME_DEFINE_GUID(CLSID_FileSystemObject, 0x0D43FE01, 0xF093, 0x11CF, 0x89, 0x40, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x28);
MIME_DEFINE_GUID(CLSID_Encoder, 0x32DA2B15, 0xCFED, 0x11D1, 0xB7, 0x47, 0x00, 0xC0, 0x4F, 0xC2, 0xB0, 0x85);
MIME_DEFINE_GUID(CLSID_TextStream, 0x0BB02EC0, 0xEF49, 0x11CF, 0x89, 0x40, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x28);
MIME_DEFINE_GUID(IID_IDictionary, 0x42C642C1, 0x97E1, 0x11CF, 0x97, 0x8F, 0x00, 0xA0, 0x24, 0x63, 0xE0, 0x6F);
MIME_DEFINE_GUID(IID_IFileSystem, 0x0AB5A3D0, 0xE5B6, 0x11D0, 0xAB, 0xF5, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(IID_IFileSystem3, 0x2A0B9D10, 0x4B87, 0x11D3, 0xA9, 0x7A, 0x00, 0x10, 0x4B, 0x36, 0x5C, 0x9F);
MIME_DEFINE_GUID(IID_IDriveCollection, 0xC7C3F5A1, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(IID_IDrive, 0xC7C3F5A0, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(IID_IFolder, 0xC7C3F5A2, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(IID_IFolderCollection, 0xC7C3F5A3, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(IID_IFile, 0xC7C3F5A4, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(IID_IFileCollection, 0xC7C3F5A5, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(IID_ITextStream, 0x53BAD8C1, 0xE718, 0x11CF, 0x89, 0x3D, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x28);
MIME_DEFINE_GUID(IID_IScriptEncoder, 0xAADC65F6, 0xCFF1, 0x11D1, 0xB7, 0x47, 0x00, 0xC0, 0x4F, 0xC2, 0xB0, 0x85);
MIME_DEFINE_GUID(IID_IProvideClassInfo, 0xB196B283, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
MIME_DEFINE_GUID(IID_IExternalConnection, 0x00000019, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

typedef enum CompareMethod { BinaryCompare = 0, TextCompare = 1, DatabaseCompare = 2 } CompareMethod;
typedef enum IOMode { ForReading = 1, ForWriting = 2, ForAppending = 8 } IOMode;
typedef enum Tristate { TristateUseDefault = -2, TristateTrue = -1, TristateFalse = 0, TristateMixed = -2 } Tristate;
typedef enum SpecialFolderConst { WindowsFolder = 0, SystemFolder = 1, TemporaryFolder = 2 } SpecialFolderConst;
typedef enum StandardStreamTypes { StdIn = 0, StdOut = 1, StdErr = 2 } StandardStreamTypes;
typedef enum DriveTypeConst {
    UnknownType = 0,
    Removable = 1,
    Fixed = 2,
    Remote = 3,
    CDRom = 4,
    RamDisk = 5
} DriveTypeConst;

typedef struct IDictionary IDictionary;
typedef struct IFileSystem3 IFileSystem3;
typedef struct IDrive IDrive;
typedef struct IDriveCollection IDriveCollection;
typedef struct IFolder IFolder;
typedef struct IFolderCollection IFolderCollection;
typedef struct IFile IFile;
typedef struct IFileCollection IFileCollection;
typedef struct ITextStream ITextStream;

#define IDictionary_METHODS(T)                                                                                         \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *putref_Item)(T * This, VARIANT * key, VARIANT * item);                                  \
    HRESULT(STDMETHODCALLTYPE *put_Item)(T * This, VARIANT * key, VARIANT * item);                                     \
    HRESULT(STDMETHODCALLTYPE *get_Item)(T * This, VARIANT * key, VARIANT * item);                                     \
    HRESULT(STDMETHODCALLTYPE *Add)(T * This, VARIANT * key, VARIANT * item);                                          \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);                                                     \
    HRESULT(STDMETHODCALLTYPE *Exists)(T * This, VARIANT * key, VARIANT_BOOL * exists);                                \
    HRESULT(STDMETHODCALLTYPE *Items)(T * This, VARIANT * items);                                                      \
    HRESULT(STDMETHODCALLTYPE *put_Key)(T * This, VARIANT * key, VARIANT * new_key);                                   \
    HRESULT(STDMETHODCALLTYPE *Keys)(T * This, VARIANT * keys);                                                        \
    HRESULT(STDMETHODCALLTYPE *Remove)(T * This, VARIANT * key);                                                       \
    HRESULT(STDMETHODCALLTYPE *RemoveAll)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *put_CompareMode)(T * This, CompareMethod mode);                                         \
    HRESULT(STDMETHODCALLTYPE *get_CompareMode)(T * This, CompareMethod * mode);                                       \
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(T * This, IUnknown * *enumerator);                                            \
    HRESULT(STDMETHODCALLTYPE *get_HashVal)(T * This, VARIANT * key, VARIANT * hash);
MIME_COM_INTERFACE(IDictionary)

#define IDrive_METHODS(T)                                                                                              \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Path)(T * This, BSTR * path);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_DriveLetter)(T * This, BSTR * letter);                                              \
    HRESULT(STDMETHODCALLTYPE *get_ShareName)(T * This, BSTR * name);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_DriveType)(T * This, DriveTypeConst * type);                                        \
    HRESULT(STDMETHODCALLTYPE *get_RootFolder)(T * This, IFolder * *folder);                                           \
    HRESULT(STDMETHODCALLTYPE *get_AvailableSpace)(T * This, VARIANT * space);                                         \
    HRESULT(STDMETHODCALLTYPE *get_FreeSpace)(T * This, VARIANT * space);                                              \
    HRESULT(STDMETHODCALLTYPE *get_TotalSize)(T * This, VARIANT * size);                                               \
    HRESULT(STDMETHODCALLTYPE *get_VolumeName)(T * This, BSTR * name);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_VolumeName)(T * This, BSTR name);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_FileSystem)(T * This, BSTR * name);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_SerialNumber)(T * This, LONG * serial);                                             \
    HRESULT(STDMETHODCALLTYPE *get_IsReady)(T * This, VARIANT_BOOL * ready);
MIME_COM_INTERFACE(IDrive)

#define IDriveCollection_METHODS(T)                                                                                    \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Item)(T * This, VARIANT key, IDrive * *drive);                                      \
    HRESULT(STDMETHODCALLTYPE *get__NewEnum)(T * This, IUnknown * *enumerator);                                        \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);
MIME_COM_INTERFACE(IDriveCollection)

#define IFolder_METHODS(T)                                                                                             \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Path)(T * This, BSTR * path);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, BSTR * name);                                                       \
    HRESULT(STDMETHODCALLTYPE *put_Name)(T * This, BSTR name);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_ShortPath)(T * This, BSTR * path);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_ShortName)(T * This, BSTR * name);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_Drive)(T * This, IDrive * *drive);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_ParentFolder)(T * This, IFolder * *folder);                                         \
    HRESULT(STDMETHODCALLTYPE *get_Attributes)(T * This, LONG * attributes);                                           \
    HRESULT(STDMETHODCALLTYPE *put_Attributes)(T * This, LONG attributes);                                             \
    HRESULT(STDMETHODCALLTYPE *get_DateCreated)(T * This, DATE * date);                                                \
    HRESULT(STDMETHODCALLTYPE *get_DateLastModified)(T * This, DATE * date);                                           \
    HRESULT(STDMETHODCALLTYPE *get_DateLastAccessed)(T * This, DATE * date);                                           \
    HRESULT(STDMETHODCALLTYPE *get_Type)(T * This, BSTR * type);                                                       \
    HRESULT(STDMETHODCALLTYPE *Delete)(T * This, VARIANT_BOOL force);                                                  \
    HRESULT(STDMETHODCALLTYPE *Copy)(T * This, BSTR destination, VARIANT_BOOL overwrite);                              \
    HRESULT(STDMETHODCALLTYPE *Move)(T * This, BSTR destination);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_IsRootFolder)(T * This, VARIANT_BOOL * root);                                       \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, VARIANT * size);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_SubFolders)(T * This, IFolderCollection * *folders);                                \
    HRESULT(STDMETHODCALLTYPE *get_Files)(T * This, IFileCollection * *files);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateTextFile)(T * This, BSTR name, VARIANT_BOOL overwrite, VARIANT_BOOL unicode,      \
                                               ITextStream * *stream);
MIME_COM_INTERFACE(IFolder)

#define IFolderCollection_METHODS(T)                                                                                   \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *Add)(T * This, BSTR name, IFolder * *folder);                                           \
    HRESULT(STDMETHODCALLTYPE *get_Item)(T * This, VARIANT key, IFolder * *folder);                                    \
    HRESULT(STDMETHODCALLTYPE *get__NewEnum)(T * This, IUnknown * *enumerator);                                        \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);
MIME_COM_INTERFACE(IFolderCollection)

#define IFile_METHODS(T)                                                                                               \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Path)(T * This, BSTR * path);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, BSTR * name);                                                       \
    HRESULT(STDMETHODCALLTYPE *put_Name)(T * This, BSTR name);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_ShortPath)(T * This, BSTR * path);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_ShortName)(T * This, BSTR * name);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_Drive)(T * This, IDrive * *drive);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_ParentFolder)(T * This, IFolder * *folder);                                         \
    HRESULT(STDMETHODCALLTYPE *get_Attributes)(T * This, LONG * attributes);                                           \
    HRESULT(STDMETHODCALLTYPE *put_Attributes)(T * This, LONG attributes);                                             \
    HRESULT(STDMETHODCALLTYPE *get_DateCreated)(T * This, DATE * date);                                                \
    HRESULT(STDMETHODCALLTYPE *get_DateLastModified)(T * This, DATE * date);                                           \
    HRESULT(STDMETHODCALLTYPE *get_DateLastAccessed)(T * This, DATE * date);                                           \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, VARIANT * size);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_Type)(T * This, BSTR * type);                                                       \
    HRESULT(STDMETHODCALLTYPE *Delete)(T * This, VARIANT_BOOL force);                                                  \
    HRESULT(STDMETHODCALLTYPE *Copy)(T * This, BSTR destination, VARIANT_BOOL overwrite);                              \
    HRESULT(STDMETHODCALLTYPE *Move)(T * This, BSTR destination);                                                      \
    HRESULT(STDMETHODCALLTYPE *OpenAsTextStream)(T * This, IOMode mode, Tristate format, ITextStream * *stream);
MIME_COM_INTERFACE(IFile)

#define IFileCollection_METHODS(T)                                                                                     \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Item)(T * This, VARIANT key, IFile * *file);                                        \
    HRESULT(STDMETHODCALLTYPE *get__NewEnum)(T * This, IUnknown * *enumerator);                                        \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);
MIME_COM_INTERFACE(IFileCollection)

#define ITextStream_METHODS(T)                                                                                         \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Line)(T * This, LONG * line);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_Column)(T * This, LONG * column);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_AtEndOfStream)(T * This, VARIANT_BOOL * end);                                       \
    HRESULT(STDMETHODCALLTYPE *get_AtEndOfLine)(T * This, VARIANT_BOOL * end);                                         \
    HRESULT(STDMETHODCALLTYPE *Read)(T * This, LONG characters, BSTR * text);                                          \
    HRESULT(STDMETHODCALLTYPE *ReadLine)(T * This, BSTR * text);                                                       \
    HRESULT(STDMETHODCALLTYPE *ReadAll)(T * This, BSTR * text);                                                        \
    HRESULT(STDMETHODCALLTYPE *Write)(T * This, BSTR text);                                                            \
    HRESULT(STDMETHODCALLTYPE *WriteLine)(T * This, BSTR text);                                                        \
    HRESULT(STDMETHODCALLTYPE *WriteBlankLines)(T * This, LONG lines);                                                 \
    HRESULT(STDMETHODCALLTYPE *Skip)(T * This, LONG characters);                                                       \
    HRESULT(STDMETHODCALLTYPE *SkipLine)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(ITextStream)

#define IFileSystem3_METHODS(T)                                                                                        \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Drives)(T * This, IDriveCollection * *drives);                                      \
    HRESULT(STDMETHODCALLTYPE *BuildPath)(T * This, BSTR path, BSTR name, BSTR * result);                              \
    HRESULT(STDMETHODCALLTYPE *GetDriveName)(T * This, BSTR path, BSTR * result);                                      \
    HRESULT(STDMETHODCALLTYPE *GetParentFolderName)(T * This, BSTR path, BSTR * result);                               \
    HRESULT(STDMETHODCALLTYPE *GetFileName)(T * This, BSTR path, BSTR * result);                                       \
    HRESULT(STDMETHODCALLTYPE *GetBaseName)(T * This, BSTR path, BSTR * result);                                       \
    HRESULT(STDMETHODCALLTYPE *GetExtensionName)(T * This, BSTR path, BSTR * result);                                  \
    HRESULT(STDMETHODCALLTYPE *GetAbsolutePathName)(T * This, BSTR path, BSTR * result);                               \
    HRESULT(STDMETHODCALLTYPE *GetTempName)(T * This, BSTR * result);                                                  \
    HRESULT(STDMETHODCALLTYPE *DriveExists)(T * This, BSTR spec, VARIANT_BOOL * exists);                               \
    HRESULT(STDMETHODCALLTYPE *FileExists)(T * This, BSTR spec, VARIANT_BOOL * exists);                                \
    HRESULT(STDMETHODCALLTYPE *FolderExists)(T * This, BSTR spec, VARIANT_BOOL * exists);                              \
    HRESULT(STDMETHODCALLTYPE *GetDrive)(T * This, BSTR spec, IDrive * *drive);                                        \
    HRESULT(STDMETHODCALLTYPE *GetFile)(T * This, BSTR path, IFile * *file);                                           \
    HRESULT(STDMETHODCALLTYPE *GetFolder)(T * This, BSTR path, IFolder * *folder);                                     \
    HRESULT(STDMETHODCALLTYPE *GetSpecialFolder)(T * This, SpecialFolderConst which, IFolder * *folder);               \
    HRESULT(STDMETHODCALLTYPE *DeleteFile)(T * This, BSTR spec, VARIANT_BOOL force);                                   \
    HRESULT(STDMETHODCALLTYPE *DeleteFolder)(T * This, BSTR spec, VARIANT_BOOL force);                                 \
    HRESULT(STDMETHODCALLTYPE *MoveFile)(T * This, BSTR source, BSTR destination);                                     \
    HRESULT(STDMETHODCALLTYPE *MoveFolder)(T * This, BSTR source, BSTR destination);                                   \
    HRESULT(STDMETHODCALLTYPE *CopyFile)(T * This, BSTR source, BSTR destination, VARIANT_BOOL overwrite);             \
    HRESULT(STDMETHODCALLTYPE *CopyFolder)(T * This, BSTR source, BSTR destination, VARIANT_BOOL overwrite);           \
    HRESULT(STDMETHODCALLTYPE *CreateFolder)(T * This, BSTR path, IFolder * *folder);                                  \
    HRESULT(STDMETHODCALLTYPE *CreateTextFile)(T * This, BSTR name, VARIANT_BOOL overwrite, VARIANT_BOOL unicode,      \
                                               ITextStream * *stream);                                                 \
    HRESULT(STDMETHODCALLTYPE *OpenTextFile)(T * This, BSTR name, IOMode mode, VARIANT_BOOL create, Tristate format,   \
                                             ITextStream * *stream);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetStandardStream)(T * This, StandardStreamTypes type, VARIANT_BOOL unicode,            \
                                                  ITextStream * *stream);                                              \
    HRESULT(STDMETHODCALLTYPE *GetFileVersion)(T * This, BSTR name, BSTR * version);
MIME_COM_INTERFACE(IFileSystem3)

typedef struct IProvideClassInfo IProvideClassInfo;
#define IProvideClassInfo_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetClassInfo)(T * This, ITypeInfo * *info);
MIME_COM_INTERFACE(IProvideClassInfo)

typedef struct IExternalConnection IExternalConnection;
#define IExternalConnection_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    DWORD(STDMETHODCALLTYPE *AddConnection)(T * This, DWORD connection, DWORD reserved);                               \
    DWORD(STDMETHODCALLTYPE *ReleaseConnection)(T * This, DWORD connection, DWORD reserved, BOOL last);
MIME_COM_INTERFACE(IExternalConnection)

/* the stream of the pipe or the console, which the script host's objects serve, exported by scrrun.dll. */
#ifndef SCRRUNAPI
#define SCRRUNAPI DECLSPEC_IMPORT
#endif
SCRRUNAPI HRESULT WINAPI DoOpenPipeStream(HANDLE pipe, IOMode mode, ITextStream **stream);

#endif
