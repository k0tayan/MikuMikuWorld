#pragma once
// No-op SAL annotation stubs for non-Windows builds. DirectXMath unconditionally
// #include "sal.h" from Microsoft's distribution; provide empty macros so the
// rest of the header compiles under clang on macOS.

#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif
#ifndef _In_reads_
#define _In_reads_(n)
#endif
#ifndef _In_reads_opt_
#define _In_reads_opt_(n)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(n)
#endif
#ifndef _Out_writes_
#define _Out_writes_(n)
#endif
#ifndef _Out_writes_opt_
#define _Out_writes_opt_(n)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(n)
#endif
#ifndef _Inout_updates_
#define _Inout_updates_(n)
#endif
#ifndef _Field_size_
#define _Field_size_(n)
#endif
#ifndef _Field_size_opt_
#define _Field_size_opt_(n)
#endif
#ifndef _Ret_
#define _Ret_
#endif
#ifndef _Success_
#define _Success_(expr)
#endif
#ifndef _When_
#define _When_(cond, annotes)
#endif
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
#endif
#ifndef _Analysis_assume_
#define _Analysis_assume_(expr)
#endif

// Do NOT stub __in / __out / __inout. libstdc++ (gcc) uses those identifiers
// for internal variable names inside <bits/locale_conv.h> and friends; macro
// expansion would turn them into empty tokens and break unrelated translation
// units. DirectXMath itself does not reference the old-style SAL annotations.
