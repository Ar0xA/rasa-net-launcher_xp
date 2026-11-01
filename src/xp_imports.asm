; Import-table entries for kernel32 functions Windows XP lacks (see xp_shims.cpp).
; Defining __imp__Name@N here means the linker takes this pointer instead of pulling
; the import from kernel32.lib, so the name never reaches the exe's import table.

.386
.model flat

EXTERN _xp_FlsAlloc@4:PROC
EXTERN _xp_FlsGetValue@4:PROC
EXTERN _xp_FlsSetValue@8:PROC
EXTERN _xp_FlsFree@4:PROC
EXTERN _xp_InitializeCriticalSectionEx@12:PROC

PUBLIC __imp__FlsAlloc@4
PUBLIC __imp__FlsGetValue@4
PUBLIC __imp__FlsSetValue@8
PUBLIC __imp__FlsFree@4
PUBLIC __imp__InitializeCriticalSectionEx@12

.data
__imp__FlsAlloc@4                    DD _xp_FlsAlloc@4
__imp__FlsGetValue@4                 DD _xp_FlsGetValue@4
__imp__FlsSetValue@8                 DD _xp_FlsSetValue@8
__imp__FlsFree@4                     DD _xp_FlsFree@4
__imp__InitializeCriticalSectionEx@12 DD _xp_InitializeCriticalSectionEx@12

END
