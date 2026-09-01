[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$enginePath = Join-Path $repositoryRoot 'src\Engine\ProcessEvent.cpp'
$bridgePath = Join-Path $repositoryRoot 'src\Coop\ProcessEventBridge.cpp'

$engine = Get-Content -LiteralPath $enginePath -Raw
$bridge = Get-Content -LiteralPath $bridgePath -Raw

$hookStart = $engine.IndexOf('static void __fastcall ProcessEvent_Hook')
$hookEnd = $engine.IndexOf('static void PatchPinballCannonPrompt', $hookStart)
if ($hookStart -lt 0 -or $hookEnd -le $hookStart) {
    throw 'Could not isolate ProcessEvent_Hook.'
}
$hook = $engine.Substring($hookStart, $hookEnd - $hookStart)

$anchors = @(
    'ProcessEventBridge::Invocation',
    'ProcessEventBridge::EarlyBefore',
    'ProcessEvent layout anchor: MadnessPatch pre-processing.',
    'ProcessEventBridge::BeforeOriginal',
    'ProcessEvent layout anchor: original call.',
    'ProcessEvent.thiscall<void>',
    'ProcessEventBridge::AfterOriginal',
    'ProcessEvent layout anchor: MadnessPatch post-processing.'
)

$previousIndex = -1
foreach ($anchor in $anchors) {
    $index = $hook.IndexOf($anchor)
    if ($index -lt 0) {
        throw "Missing ProcessEvent layout anchor: $anchor"
    }
    if ($index -le $previousIndex) {
        throw "ProcessEvent layout anchor is out of order: $anchor"
    }
    $previousIndex = $index
}

if ([regex]::Matches($hook, [regex]::Escape('ProcessEvent.thiscall<void>')).Count -ne 1) {
    throw 'ProcessEvent_Hook must contain exactly one original ProcessEvent call.'
}
if ([regex]::Matches($hook, '(?m)^\s*return;\s*$').Count -ne 1) {
    throw 'ProcessEvent_Hook must contain exactly one terminal return site.'
}

$forbiddenEngineCallbacks = @(
    'AliceCoop::HandleSharedCombatProcessEvent',
    'AliceCoop::TraceLifecycleProcessEvent',
    'AliceCoop::TraceWorldProcessEvent',
    'AliceCoop::ShouldDeferSequenceOpActivation',
    'AliceCoop::OnLocalPepperProjectileSpawn',
    'AliceCoop::OnLocalClockBombSpawn'
)
foreach ($callback in $forbiddenEngineCallbacks) {
    if ($engine.Contains($callback)) {
        throw "AliceCoop ProcessEvent callback leaked into Engine: $callback"
    }
}

if ($engine -match '\bg_coop[A-Za-z0-9_]*\b') {
    throw 'AliceCoop-exclusive g_coop registry symbols must not remain in Engine.'
}

$forbiddenEngineGlobals = @(
    'g_tickAlicePawn',
    'g_aliceHudPostRender',
    'g_viewportPostRender',
    'g_projectileInit',
    'g_projectilePostBeginPlay',
    'g_rangeProjectileFire',
    'g_clockBombSetupStart',
    'g_clockBombDetonate',
    'g_clockBombDestroyed',
    'g_pcSetupClockBomb',
    'g_pcDetonateClockBomb'
)
foreach ($symbol in $forbiddenEngineGlobals) {
    if ($engine -match "\b$symbol\b") {
        throw "AliceCoop-exclusive registry symbol leaked into Engine: $symbol"
    }
}

if ($bridge.Contains('ProcessEvent.thiscall')) {
    throw 'ProcessEventBridge must not call the original ProcessEvent.'
}
if ($bridge -match '(?i)\bsafetyhook\b') {
    throw 'ProcessEventBridge must not own or reference SafetyHook.'
}

Write-Output 'ProcessEvent bridge layout is valid.'
