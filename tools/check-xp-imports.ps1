# Fails if the exe imports a function Windows XP (32-bit, SP3) does not have, or if its
# PE header asks for a newer Windows. Run from a Visual Studio x86 prompt (needs dumpbin).
#
#   .\tools\check-xp-imports.ps1 build\InfiniteRasaLauncherXP.exe
param([Parameter(Mandatory)] [string]$Exe)
$ErrorActionPreference = 'Stop'

# Functions the toolchain or Windows SDK tends to pull in that arrived after XP.
$notOnXp = @(
    'FlsAlloc', 'FlsFree', 'FlsGetValue', 'FlsSetValue', 'InitializeCriticalSectionEx',
    'InitOnceBeginInitialize', 'InitOnceComplete', 'InitOnceExecuteOnce', 'InitOnceInitialize',
    'InitializeSRWLock', 'AcquireSRWLockExclusive', 'AcquireSRWLockShared', 'ReleaseSRWLockExclusive',
    'ReleaseSRWLockShared', 'TryAcquireSRWLockExclusive', 'TryAcquireSRWLockShared',
    'InitializeConditionVariable', 'SleepConditionVariableCS', 'SleepConditionVariableSRW',
    'WakeConditionVariable', 'WakeAllConditionVariable',
    'GetTickCount64', 'GetSystemTimePreciseAsFileTime', 'CreateEventExW', 'CreateEventExA', 'CreateMutexExW',
    'CreateSemaphoreExW', 'CreateWaitableTimerExW', 'GetFileInformationByHandleEx', 'SetFileInformationByHandle',
    'GetFinalPathNameByHandleW', 'CreateSymbolicLinkW', 'CancelIoEx', 'CreateFile2',
    'GetLocaleInfoEx', 'LCMapStringEx', 'CompareStringEx', 'GetUserDefaultLocaleName', 'LocaleNameToLCID',
    'LCIDToLocaleName', 'EnumSystemLocalesEx', 'IsValidLocaleName', 'GetDateFormatEx', 'GetTimeFormatEx',
    'GetDynamicTimeZoneInformation', 'GetCurrentProcessorNumber', 'FlushProcessWriteBuffers',
    'QueryFullProcessImageNameW', 'GetThreadId', 'SetThreadStackGuarantee', 'GetErrorMode',
    'SetDefaultDllDirectories', 'AddDllDirectory', 'RemoveDllDirectory', 'Wow64DisableWow64FsRedirection',
    'Wow64RevertWow64FsRedirection', 'CreateThreadpoolWork', 'CreateThreadpoolTimer', 'CreateThreadpoolWait',
    'SubmitThreadpoolWork', 'CloseThreadpoolWork', 'GetEnabledXStateFeatures', 'SetThreadDescription',
    'RegGetValueW', 'RegDeleteTreeW', 'RegSetKeyValueW', 'RegDeleteKeyExW',
    'SHGetKnownFolderPath', 'SHCreateItemFromParsingName', 'TaskDialog', 'TaskDialogIndirect',
    'SetProcessDPIAware', 'GetDpiForWindow', 'GetDpiForSystem', 'ChangeWindowMessageFilter',
    'inet_pton', 'inet_ntop', 'InetPtonW', 'InetNtopW', 'WSAPoll',
    'BCryptGenRandom', 'BCryptOpenAlgorithmProvider'
)
# DLLs that do not exist on XP at all.
$dllsNotOnXp = @('bcrypt.dll', 'api-ms-win-', 'ucrtbase.dll', 'vcruntime140.dll', 'msvcp140.dll', 'dwmapi.dll', 'shcore.dll')

$headers = & dumpbin /nologo /headers $Exe | Out-String
$imports = & dumpbin /nologo /imports $Exe
$problems = @()

if ($headers -notmatch '5\.01 operating system version') { $problems += 'PE operating system version is not 5.01' }
if ($headers -notmatch '5\.01 subsystem version') { $problems += 'PE subsystem version is not 5.01' }
if ($headers -notmatch 'machine \(x86\)') { $problems += 'not an x86 (32-bit) exe' }

$dll = ''
$names = @()
foreach ($line in $imports) {
    if ($line -match '^\s{4}(\S+\.dll)\s*$') { $dll = $Matches[1].ToLower(); continue }
    if ($line -match '^\s+[0-9A-F]+\s+(\w+)\s*$') {
        $names += "$dll!$($Matches[1])"
        if ($notOnXp -contains $Matches[1]) { $problems += "$dll imports $($Matches[1]), which XP lacks" }
    }
}
foreach ($d in ($names | ForEach-Object { $_.Split('!')[0] } | Sort-Object -Unique)) {
    foreach ($bad in $dllsNotOnXp) { if ($d.StartsWith($bad)) { $problems += "imports from $d, which XP lacks" } }
}

Write-Host "$($names.Count) imports from $(($names | ForEach-Object { $_.Split('!')[0] } | Sort-Object -Unique) -join ', ')"
if ($problems) {
    $problems | ForEach-Object { Write-Host "XP CHECK FAILED: $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'XP check passed: PE header targets 5.01 and no known post-XP imports.' -ForegroundColor Green
