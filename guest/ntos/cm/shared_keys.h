/*
 * the subtrees of HKLM\SOFTWARE that the 32-bit and the 64-bit view share. every other key of a 32-bit program is below Wow6432Node.
 * the registry views and the hive compiler read this list, paths are relative to SOFTWARE.
 */
#ifndef MIME_CM_SHARED_KEYS_H
#define MIME_CM_SHARED_KEYS_H

#define CM_SHARED_SOFTWARE_KEYS(X) \
    X("\\Classes") \
    X("\\Clients") \
    X("\\Policies") \
    X("\\RegisteredApplications") \
    X("\\Microsoft\\COM3") \
    X("\\Microsoft\\Cryptography\\Calais\\Current") \
    X("\\Microsoft\\Cryptography\\Calais\\Readers") \
    X("\\Microsoft\\Cryptography\\Services") \
    X("\\Microsoft\\CTF\\SystemShared") \
    X("\\Microsoft\\CTF\\TIP") \
    X("\\Microsoft\\DFS") \
    X("\\Microsoft\\Driver Signing") \
    X("\\Microsoft\\EnterpriseCertificates") \
    X("\\Microsoft\\EventSystem") \
    X("\\Microsoft\\MSMQ") \
    X("\\Microsoft\\Non-Driver Signing") \
    X("\\Microsoft\\Notepad\\DefaultFonts") \
    X("\\Microsoft\\OLE") \
    X("\\Microsoft\\RAS") \
    X("\\Microsoft\\RPC") \
    X("\\Microsoft\\SOFTWARE\\Microsoft\\Shared Tools\\MSInfo") \
    X("\\Microsoft\\SystemCertificates") \
    X("\\Microsoft\\TermServLicensing") \
    X("\\Microsoft\\TransactionServer") \
    X("\\Microsoft\\Windows\\CurrentVersion\\App Paths") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cursors\\Schemes") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Explorer\\DriveIcons") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Group Policy") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Policies") \
    X("\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Setup") \
    X("\\Microsoft\\Windows\\CurrentVersion\\Telephony\\Locations") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Console") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\FontDpi") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\FontLink") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\FontMapper") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Fonts") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Gre_Initialize") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Language Pack") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\NetworkCards") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Perflib") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Ports") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Print") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList") \
    X("\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones")

/* the keys of Classes that each view has its own copy of, below Classes\Wow6432Node. */
#define CM_REDIRECTED_CLASSES(X) X("CLSID") X("DirectShow") X("Interface") X("Media Type") X("MediaFoundation")

#endif
