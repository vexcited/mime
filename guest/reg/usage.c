/*
 * the help of reg.exe: "reg /?" and "reg <operation> /?".
 */
#include "reg.h"

static const WCHAR k_general[] =
    L"REG Operation [Parameter List]\n"
    L"\n"
    L"  Operation  [ QUERY   | ADD    | DELETE  | COPY    |\n"
    L"               SAVE    | LOAD   | UNLOAD  | RESTORE |\n"
    L"               COMPARE | EXPORT | IMPORT  | FLAGS ]\n"
    L"\n"
    L"Return Code: (Except for REG COMPARE)\n"
    L"\n"
    L"  0 - Successful\n"
    L"  1 - Failed\n"
    L"\n"
    L"For help on a specific operation type:\n"
    L"\n"
    L"  REG Operation /?\n"
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG QUERY /?\n"
    L"  REG ADD /?\n"
    L"  REG DELETE /?\n"
    L"  REG COPY /?\n"
    L"  REG SAVE /?\n"
    L"  REG RESTORE /?\n"
    L"  REG LOAD /?\n"
    L"  REG UNLOAD /?\n"
    L"  REG COMPARE /?\n"
    L"  REG EXPORT /?\n"
    L"  REG IMPORT /?\n"
    L"  REG FLAGS /?\n";

#define KEY_HELP_MACHINE                                                                                         \
    L"  KeyName    [\\\\Machine\\]FullKey\n"                                                                       \
    L"    Machine  The name of a remote machine; this machine when left out. Only HKLM and HKU\n"                \
    L"             can be used on a remote machine.\n"                                                            \
    L"    FullKey  ROOTKEY\\SubKey\n"                                                                             \
    L"    ROOTKEY  [ HKLM | HKCU | HKCR | HKU | HKCC ]\n"                                                         \
    L"    SubKey   The full name of a key below the root key.\n"

#define VIEW_HELP                                                                                                \
    L"  /reg:32    Use the 32-bit view of the registry.\n"                                                       \
    L"\n"                                                                                                        \
    L"  /reg:64    Use the 64-bit view of the registry.\n"

static const WCHAR k_query[] =
    L"REG QUERY KeyName [/v [ValueName] | /ve] [/s]\n"
    L"          [/f Data [/k] [/d] [/c] [/e]] [/t Type] [/z] [/se Separator]\n"
    L"          [/reg:32 | /reg:64]\n"
    L"\n" KEY_HELP_MACHINE
    L"\n"
    L"  /v       Shows the value with this name. All the values of the key when left out.\n"
    L"           The name may be left out when /f is given: only value names are searched.\n"
    L"           A name may hold the wildcards * and ?.\n"
    L"\n"
    L"  /ve      Shows the default value, the one with an empty name.\n"
    L"\n"
    L"  /s       Goes through all the subkeys, like dir /s.\n"
    L"\n"
    L"  /se      The one character that separates the strings of a REG_MULTI_SZ. \\0 when left out.\n"
    L"\n"
    L"  /f       The data or pattern to search for; * and ? are wildcards. Quote it when it has\n"
    L"           spaces. * when left out.\n"
    L"\n"
    L"  /k       Search key names only.\n"
    L"\n"
    L"  /d       Search data only.\n"
    L"\n"
    L"  /c       The search is case sensitive. It is not by default.\n"
    L"\n"
    L"  /e       Only exact matches. All matches by default.\n"
    L"\n"
    L"  /t       Shows only values of these types, separated by commas:\n"
    L"             REG_SZ, REG_MULTI_SZ, REG_EXPAND_SZ,\n"
    L"             REG_DWORD, REG_QWORD, REG_BINARY, REG_NONE\n"
    L"           All types by default.\n"
    L"\n"
    L"  /z       Shows the number of each type as well.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG QUERY HKLM\\Software\\Microsoft\\ResKit /v Version\n"
    L"    Shows the value Version\n"
    L"\n"
    L"  REG QUERY HKLM\\Software\\Microsoft\\ResKit\\Nt\\Setup /s\n"
    L"    Shows all the subkeys and values of the key Setup\n"
    L"\n"
    L"  REG QUERY HKLM\\Software\\Microsoft\\ResKit\\Nt\\Setup /se #\n"
    L"    Shows them with # between the strings of a REG_MULTI_SZ\n"
    L"\n"
    L"  REG QUERY HKLM /f SYSTEM /t REG_SZ /s\n"
    L"    Looks in the machine key for values of the type REG_SZ whose name or data holds SYSTEM\n"
    L"\n"
    L"  REG QUERY HKCU /f 0x2 /d /t REG_DWORD /s /e\n"
    L"    Looks in the user key for REG_DWORD data equal to 0x2\n";

static const WCHAR k_add[] =
    L"REG ADD KeyName [/v ValueName | /ve] [/t Type] [/s Separator] [/d Data] [/f]\n"
    L"        [/reg:32 | /reg:64]\n"
    L"\n" KEY_HELP_MACHINE
    L"\n"
    L"  /v       The name of the value to add.\n"
    L"\n"
    L"  /ve      Adds a value with an empty name, the default value of the key.\n"
    L"\n"
    L"  /t       The type of the value:\n"
    L"             [ REG_SZ    | REG_MULTI_SZ | REG_EXPAND_SZ |\n"
    L"               REG_DWORD | REG_QWORD    | REG_BINARY    | REG_NONE ]\n"
    L"           REG_SZ when left out.\n"
    L"\n"
    L"  /s       The one character that separates the strings of the data of a REG_MULTI_SZ.\n"
    L"           \\0 when left out.\n"
    L"\n"
    L"  /d       The data of the value.\n"
    L"\n"
    L"  /f       Replaces an existing value without asking.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG ADD \\\\ABC\\HKLM\\Software\\MyCo\n"
    L"    Adds the key HKLM\\Software\\MyCo on the machine ABC\n"
    L"\n"
    L"  REG ADD HKLM\\Software\\MyCo /v Data /t REG_BINARY /d fe340ead\n"
    L"    Adds a value Data of the type REG_BINARY with the data fe340ead\n"
    L"\n"
    L"  REG ADD HKLM\\Software\\MyCo /v MRU /t REG_MULTI_SZ /d fax\\0mail\n"
    L"    Adds a value MRU of the type REG_MULTI_SZ with the data fax\\0mail\\0\\0\n"
    L"\n"
    L"  REG ADD HKLM\\Software\\MyCo /v Path /t REG_EXPAND_SZ /d ^%systemroot^%\n"
    L"    Adds a value Path of the type REG_EXPAND_SZ with the data %systemroot%\n"
    L"    Notice: put a caret ( ^ ) before each percent sign in the data\n";

static const WCHAR k_delete[] =
    L"REG DELETE KeyName [/v ValueName | /ve | /va] [/f] [/reg:32 | /reg:64]\n"
    L"\n" KEY_HELP_MACHINE
    L"\n"
    L"  ValueName  The name of the value to delete. Without it the key is deleted with all its\n"
    L"             subkeys and values.\n"
    L"\n"
    L"  /ve        Deletes the default value, the one with an empty name.\n"
    L"\n"
    L"  /va        Deletes all the values of the key.\n"
    L"\n"
    L"  /f         Deletes without asking.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG DELETE HKLM\\Software\\MyCo\\MyApp\\Timeout\n"
    L"    Deletes the key Timeout with all its subkeys and values\n"
    L"\n"
    L"  REG DELETE \\\\ZODIAC\\HKLM\\Software\\MyCo /v MTU\n"
    L"    Deletes the value MTU of the key MyCo on ZODIAC\n";

static const WCHAR k_copy[] =
    L"REG COPY KeyName1 KeyName2 [/s] [/f] [/reg:32 | /reg:64]\n"
    L"\n" KEY_HELP_MACHINE
    L"\n"
    L"  /s         Copies the subkeys and their values too.\n"
    L"\n"
    L"  /f         Copies without asking.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG COPY HKLM\\Software\\MyCo\\MyApp HKLM\\Software\\MyCo\\SaveMyApp /s\n"
    L"    Copies all the subkeys and values of the key MyApp to the key SaveMyApp\n"
    L"\n"
    L"  REG COPY \\\\ZODIAC\\HKLM\\Software\\MyCo HKLM\\Software\\MyCo1\n"
    L"    Copies the values of the key MyCo on ZODIAC to the key MyCo1 of this machine\n";

static const WCHAR k_save[] =
    L"REG SAVE KeyName FileName [/y] [/reg:32 | /reg:64]\n"
    L"\n"
    L"  KeyName    ROOTKEY\\SubKey\n"
    L"    ROOTKEY  [ HKLM | HKCU | HKCR | HKU | HKCC ]\n"
    L"    SubKey   The full name of a key below the root key.\n"
    L"\n"
    L"  FileName   The file to write. A name without a folder is made in the current folder.\n"
    L"\n"
    L"  /y         Replaces an existing file without asking.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG SAVE HKLM\\Software\\MyCo\\MyApp AppBkUp.hiv\n"
    L"    Saves the key MyApp as the hive file AppBkUp.hiv in the current folder\n";

static const WCHAR k_restore[] =
    L"REG RESTORE KeyName FileName [/reg:32 | /reg:64]\n"
    L"\n"
    L"  KeyName    ROOTKEY\\SubKey (this machine only)\n"
    L"    ROOTKEY  [ HKLM | HKCU | HKCR | HKU | HKCC ]\n"
    L"    SubKey   The full name of the key to restore the hive file into. Its values and subkeys\n"
    L"             are replaced.\n"
    L"\n"
    L"  FileName   The hive file to restore. It must have been made by REG SAVE.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG RESTORE HKLM\\Software\\Microsoft\\ResKit NTRKBkUp.hiv\n"
    L"    Restores the file NTRKBkUp.hiv over the key ResKit\n";

static const WCHAR k_load[] =
    L"REG LOAD KeyName FileName [/reg:32 | /reg:64]\n"
    L"\n"
    L"  KeyName    ROOTKEY\\SubKey (this machine only)\n"
    L"    ROOTKEY  [ HKLM | HKU ]\n"
    L"    SubKey   The name of the new key the hive file is loaded into.\n"
    L"\n"
    L"  FileName   The hive file to load. It must have been made by REG SAVE.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG LOAD HKLM\\TempHive TempHive.hiv\n"
    L"    Loads the file TempHive.hiv as the key TempHive of HKLM\n";

static const WCHAR k_unload[] =
    L"REG UNLOAD KeyName\n"
    L"\n"
    L"  KeyName    ROOTKEY\\SubKey (this machine only)\n"
    L"    ROOTKEY  [ HKLM | HKU ]\n"
    L"    SubKey   The name of the loaded hive to unload.\n"
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG UNLOAD HKLM\\TempHive\n"
    L"    Unloads the hive TempHive of HKLM\n";

static const WCHAR k_compare[] =
    L"REG COMPARE KeyName1 KeyName2 [/v ValueName | /ve] [Output] [/s]\n"
    L"            [/reg:32 | /reg:64]\n"
    L"\n"
    L"  KeyName    [\\\\Machine\\]FullKey\n"
    L"    Machine  The name of a remote machine; this machine when left out. Only HKLM and HKU\n"
    L"             can be used on a remote machine.\n"
    L"    FullKey  ROOTKEY\\SubKey\n"
    L"             When the second name is only a machine, the same key on that machine is meant.\n"
    L"    ROOTKEY  [ HKLM | HKCU | HKCR | HKU | HKCC ]\n"
    L"    SubKey   The full name of a key below the root key.\n"
    L"\n"
    L"  ValueName  The name of the one value to compare. All the values of the key by default.\n"
    L"\n"
    L"  /ve        Compares the default value, the one with an empty name.\n"
    L"\n"
    L"  /s         Compares the subkeys and their values too.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"  Output     [/oa | /od | /os | /on]\n"
    L"             What differs is shown by default.\n"
    L"    /oa      Show the differences and the matches.\n"
    L"    /od      Show the differences only.\n"
    L"    /os      Show the matches only.\n"
    L"    /on      Show nothing.\n"
    L"\n"
    L"Return Code:\n"
    L"\n"
    L"  0 - Successful, the keys are identical\n"
    L"  1 - Failed\n"
    L"  2 - Successful, the keys differ\n"
    L"\n"
    L"Note:\n"
    L"  The symbol at the start of each line means:\n"
    L"  = the data of KeyName1 and KeyName2 is equal\n"
    L"  < the data is from KeyName1 and differs from KeyName2\n"
    L"  > the data is from KeyName2 and differs from KeyName1\n"
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG COMPARE HKLM\\Software\\MyCo\\MyApp HKLM\\Software\\MyCo\\SaveMyApp\n"
    L"    Compares all the values of the key MyApp with those of SaveMyApp\n"
    L"\n"
    L"  REG COMPARE HKLM\\Software\\MyCo HKLM\\Software\\MyCo1 /v Version\n"
    L"    Compares the value Version of the keys MyCo and MyCo1\n"
    L"\n"
    L"  REG COMPARE \\\\ZODIAC\\HKLM\\Software\\MyCo \\\\. /s\n"
    L"    Compares HKLM\\Software\\MyCo and everything below it on ZODIAC with this machine\n";

static const WCHAR k_export[] =
    L"REG EXPORT KeyName FileName [/y] [/reg:32 | /reg:64]\n"
    L"\n"
    L"  KeyName    ROOTKEY[\\SubKey] (this machine only)\n"
    L"    ROOTKEY  [ HKLM | HKCU | HKCR | HKU | HKCC ]\n"
    L"    SubKey   The full name of a key below the root key.\n"
    L"\n"
    L"  FileName   The .reg file to write.\n"
    L"\n"
    L"  /y         Replaces an existing file without asking.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG EXPORT HKLM\\Software\\MyCo\\MyApp AppBkUp.reg\n"
    L"    Exports all the subkeys and values of the key MyApp to the file AppBkUp.reg\n";

static const WCHAR k_import[] =
    L"REG IMPORT FileName [/reg:32 | /reg:64]\n"
    L"\n"
    L"  FileName  The .reg file to import (this machine only).\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG IMPORT AppBkUp.reg\n"
    L"    Imports the entries of the file AppBkUp.reg\n";

static const WCHAR k_flags[] =
    L"REG FLAGS KeyName [QUERY |\n"
    L"                   SET [DONT_VIRTUALIZE] [DONT_SILENT_FAIL] [RECURSE_FLAG]]\n"
    L"                  [/reg:32 | /reg:64]\n"
    L"\n"
    L"  KeyName    \"HKLM\\Software\"[\\SubKey] (this machine, and only these keys)\n"
    L"    SubKey   The full name of a key below HKLM\\Software.\n"
    L"  DONT_VIRTUALIZE DONT_SILENT_FAIL RECURSE_FLAG\n"
    L"    Used with SET: the flags that are given are set and the others are cleared.\n"
    L"\n"
    L"  /s         Does the same for the subkeys.\n"
    L"\n" VIEW_HELP
    L"\n"
    L"Examples:\n"
    L"\n"
    L"  REG FLAGS HKLM\\Software\\MyCo\\MyApp QUERY\n"
    L"    Shows the flags of the key MyApp\n"
    L"\n"
    L"  REG FLAGS HKLM\\Software\\MyCo\\MyApp SET DONT_VIRTUALIZE /s\n"
    L"    Sets DONT_VIRTUALIZE and clears DONT_SILENT_FAIL and RECURSE_FLAG on MyApp and its subkeys\n";

void Usage(int operation)
{
    static const WCHAR *const texts[OP_COUNT] = {k_query,   k_add,    k_delete, k_copy,   k_save,   k_restore,
                                                 k_load,    k_unload, k_compare, k_export, k_import, k_flags};
    OutText(L"\n");
    OutText(operation >= 0 && operation < OP_COUNT ? texts[operation] : k_general);
}
